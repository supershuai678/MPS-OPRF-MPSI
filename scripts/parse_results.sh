#!/bin/bash
# Parse benchmark logs and generate unified CSV for Table 5
#
# Usage: bash scripts/parse_results.sh [log_dir] [output_csv]
#
# Reads log files from logs/ directory, extracts Time and Comm metrics,
# and outputs a unified CSV.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="${1:-$PROJECT_DIR/logs}"
OUTPUT="${2:-$PROJECT_DIR/table5_results.csv}"

if [ ! -d "$LOG_DIR" ]; then
    echo "[ERROR] Log directory not found: $LOG_DIR"
    exit 1
fi

echo "Parsing logs from: $LOG_DIR"
echo "Output CSV:        $OUTPUT"
echo ""

# CSV header — includes original sender time and customer-facing sender active time (Comm in KB)
echo "Setting,Protocol,m,n,t,Receiver_Time_s,Receiver_Active_Time_s,Receiver_Comm_KB,Leader_Time_s,Leader_Comm_KB,Max_Sender_Time_s,Max_Sender_ClientActive_Time_s,Max_Sender_Comm_KB,Total_Time_s,Total_Comm_KB" > "$OUTPUT"

extract_pipe_metric() {
    local line="$1"
    local key="$2"
    awk -v field_name="$key" -F ' \\| ' '
        {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ ("^" field_name ": ")) {
                    value = $i
                    sub("^" field_name ": ", "", value)
                    sub(/s$/, "", value)
                    sub(/ KB$/, "", value)
                    sub(/ MB$/, "", value)
                    print value
                    exit
                }
            }
        }
    ' <<< "$line"
}

parse_our_protocol() {
    local log_file="$1"
    local setting="$2"
    local proto="$3"
    local nn="$4"
    local n="$5"
    local t="$6"

    # Extract Receiver line
    local recv_line
    recv_line=$(grep "Role: Receiver" "$log_file" 2>/dev/null | head -1)
    if [ -z "$recv_line" ]; then
        echo "$setting,$proto,2^$nn,$n,$t,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL" >> "$OUTPUT"
        return
    fi

    local recv_time recv_active recv_comm
    recv_time=$(extract_pipe_metric "$recv_line" "Time")
    recv_active=$(extract_pipe_metric "$recv_line" "ReceiverActive")
    recv_comm=$(extract_pipe_metric "$recv_line" "Comm")

    # Extract Leader line
    local leader_line
    leader_line=$(grep "Role: Leader" "$log_file" 2>/dev/null | head -1)
    local leader_time leader_comm
    leader_time=$(extract_pipe_metric "$leader_line" "Time")
    leader_comm=$(extract_pipe_metric "$leader_line" "Comm")

    # Extract max Sender time and comm
    local max_sender_time="0" max_sender_client_active="N/A" max_sender_comm="0"
    while IFS= read -r sender_line; do
        local s_time s_client_active s_comm
        s_time=$(extract_pipe_metric "$sender_line" "Time")
        s_client_active=$(extract_pipe_metric "$sender_line" "ClientActive")
        s_comm=$(extract_pipe_metric "$sender_line" "Comm")
        if [ -n "$s_time" ] && [ "$(echo "$s_time > $max_sender_time" | bc 2>/dev/null)" = "1" ]; then
            max_sender_time="$s_time"
        fi
        if [ -n "$s_client_active" ]; then
            if [ "$max_sender_client_active" = "N/A" ] || [ "$(echo "$s_client_active > $max_sender_client_active" | bc 2>/dev/null)" = "1" ]; then
                max_sender_client_active="$s_client_active"
            fi
        fi
        if [ -n "$s_comm" ] && [ "$(echo "$s_comm > $max_sender_comm" | bc 2>/dev/null)" = "1" ]; then
            max_sender_comm="$s_comm"
        fi
    done < <(grep -E "Role: Sender|Role: S_" "$log_file" 2>/dev/null)

    # Total time = max of all parties; Total comm = sum of all parties
    local total_time total_comm
    total_time=$(awk "BEGIN { t=$recv_time; if ($leader_time>t) t=$leader_time; if ($max_sender_time>t) t=$max_sender_time; printf \"%.3f\", t }" 2>/dev/null || echo "$recv_time")
    total_comm=$(awk "BEGIN { printf \"%.2f\", ${recv_comm:-0} + ${leader_comm:-0} + ${max_sender_comm:-0} }" 2>/dev/null || echo "$recv_comm")

    echo "$setting,$proto,2^$nn,$n,$t,${recv_time:-N/A},${recv_active:-N/A},${recv_comm:-N/A},${leader_time:-N/A},${leader_comm:-N/A},${max_sender_time:-0},${max_sender_client_active:-N/A},${max_sender_comm:-0},$total_time,$total_comm" >> "$OUTPUT"
}

parse_bzs() {
    local log_file="$1"
    local setting="$2"
    local nn="$3"
    local n="$4"
    local t="$5"

    # BZS-MPSI outputs for three parties:
    #   n-1 = receiver (gets intersection result)
    #   n-2 = leader/pivot
    #   n-3 = sender (representative)
    # Each outputs: User_Id:X / Comm: Y.ZMB / oc::Timer with "end <time_ms> ..."
    local receiver_id=$((n - 1))
    local leader_id=$((n - 2))
    local sender_id=$((n - 3))

    # Helper: extract time (ms→s) for a given User_Id
    extract_bzs_time() {
        local target_id="$1"
        awk -v rid="$target_id" '
            /^User_Id:/ { current_id = substr($0, 9)+0; found = 0 }
            current_id == rid && /^end/ && !found {
                for (i = 2; i <= NF; i++) {
                    if ($i ~ /^[0-9.]+$/) { last = $i; break }
                }
                if (last+0 > 0) {
                    printf "%.3f", last / 1000
                    found = 1
                    exit
                }
            }
        ' "$log_file" 2>/dev/null
    }

    # Helper: extract comm (MB) for a given User_Id
    extract_bzs_comm() {
        local target_id="$1"
        awk -v rid="$target_id" '
            /^User_Id:/ { current_id = substr($0, 9)+0 }
            /^Comm:/ && current_id == rid {
                gsub(/Comm: /, ""); gsub(/MB/, "");
                print $0; exit
            }
        ' "$log_file" 2>/dev/null
    }

    # Extract all three parties
    local recv_time_s recv_comm_mb leader_time_s leader_comm_mb sender_time_s sender_comm_mb
    recv_time_s=$(extract_bzs_time "$receiver_id")
    recv_comm_mb=$(extract_bzs_comm "$receiver_id")
    leader_time_s=$(extract_bzs_time "$leader_id")
    leader_comm_mb=$(extract_bzs_comm "$leader_id")
    sender_time_s=$(extract_bzs_time "$sender_id")
    sender_comm_mb=$(extract_bzs_comm "$sender_id")

    if [ -z "$recv_time_s" ] && [ -z "$recv_comm_mb" ]; then
        echo "$setting,bzs,2^$nn,$n,$t,FAIL,N/A,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL" >> "$OUTPUT"
        return
    fi

    # Convert MB to KB
    local recv_comm_kb leader_comm_kb sender_comm_kb total_comm_kb
    [ -n "$recv_comm_mb" ] && recv_comm_kb=$(awk "BEGIN { printf \"%.2f\", $recv_comm_mb * 1024 }")
    [ -n "$leader_comm_mb" ] && leader_comm_kb=$(awk "BEGIN { printf \"%.2f\", $leader_comm_mb * 1024 }")
    [ -n "$sender_comm_mb" ] && sender_comm_kb=$(awk "BEGIN { printf \"%.2f\", $sender_comm_mb * 1024 }")
    total_comm_kb=$(awk "BEGIN { printf \"%.2f\", ${recv_comm_mb:-0} * 1024 + ${leader_comm_mb:-0} * 1024 + ${sender_comm_mb:-0} * 1024 }")

    # Total time = receiver time (receiver is last to finish, computes intersection)
    local total_time_s="${recv_time_s:-N/A}"

    echo "$setting,bzs,2^$nn,$n,$t,${recv_time_s:-N/A},N/A,${recv_comm_kb:-N/A},${leader_time_s:-N/A},${leader_comm_kb:-N/A},${sender_time_s:-N/A},N/A,${sender_comm_kb:-N/A},$total_time_s,${total_comm_kb:-N/A}" >> "$OUTPUT"
}

parse_oring() {
    local log_file="$1"
    local setting="$2"
    local proto="$3"
    local nn="$4"
    local n="$5"
    local t="$6"

    # O-Ring: each party independently outputs its own time.
    # Log contains all n parties (all processes write to same log file):
    #   "Party idx: X"
    #   "The total time (ms): 1234"
    #   "Total (MB): 5.67"
    #
    # Receiver_Time = party 1's time (single-user perspective requested by customer)
    # Total_Time = party 0's time (customer treats ID=0 as overall time)

    # Extract all parties' times and comms at once
    local all_data
    all_data=$(awk '
        /Party idx:/ {
            party_id = $NF + 0
        }
        /The total time \(ms\):/ {
            for (i=1; i<=NF; i++) {
                if ($i ~ /^[0-9.]+$/) t = $i
            }
            times[party_id] = t
        }
        /Total \(MB\):/ {
            for (i=1; i<=NF; i++) {
                if ($i ~ /^[0-9.]+$/) c = $i
            }
            comms[party_id] = c
        }
        END {
            total_comm = 0; p0_time = ""; p0_comm = ""; p1_time = ""; p1_comm = ""
            for (id in times) {
                total_comm += comms[id]+0
                if (id+0 == 0) { p0_time = times[id]; p0_comm = comms[id] }
                if (id+0 == 1) { p1_time = times[id]; p1_comm = comms[id] }
            }
            printf "%s %s %s %s %s", p0_time, p0_comm, p1_time, p1_comm, total_comm
        }
    ' "$log_file" 2>/dev/null)

    local p0_time_ms p0_comm_mb p1_time_ms p1_comm_mb total_comm_mb
    p0_time_ms=$(echo "$all_data" | awk '{print $1}')
    p0_comm_mb=$(echo "$all_data" | awk '{print $2}')
    p1_time_ms=$(echo "$all_data" | awk '{print $3}')
    p1_comm_mb=$(echo "$all_data" | awk '{print $4}')
    total_comm_mb=$(echo "$all_data" | awk '{print $5}')

    if [ -z "$p0_time_ms" ] && [ -z "$p1_time_ms" ]; then
        echo "$setting,$proto,2^$nn,$n,$t,FAIL,N/A,FAIL,N/A,N/A,N/A,N/A,N/A,FAIL,FAIL" >> "$OUTPUT"
        return
    fi

    # Convert: party 1 time (single-user) → Receiver_Time
    local p1_time_s="N/A" p1_comm_kb="N/A"
    [ -n "$p1_time_ms" ] && p1_time_s=$(awk "BEGIN { printf \"%.3f\", $p1_time_ms / 1000 }")
    [ -n "$p1_comm_mb" ] && p1_comm_kb=$(awk "BEGIN { printf \"%.2f\", $p1_comm_mb * 1024 }")

    # Convert: party 0 time → Total_Time
    local total_time_s="N/A" total_comm_kb="N/A"
    [ -n "$p0_time_ms" ] && total_time_s=$(awk "BEGIN { printf \"%.3f\", $p0_time_ms / 1000 }")
    [ -n "$total_comm_mb" ] && total_comm_kb=$(awk "BEGIN { printf \"%.2f\", $total_comm_mb * 1024 }")

    echo "$setting,$proto,2^$nn,$n,$t,$p1_time_s,N/A,$p1_comm_kb,N/A,N/A,N/A,N/A,N/A,$total_time_s,$total_comm_kb" >> "$OUTPUT"
}

parse_mpsi_paxos() {
    local log_file="$1"
    local setting="$2"
    local nn="$3"
    local n="$4"
    local t="$5"

    # mPSI-paxos (CCS 2021) outputs for 3 roles:
    #   "Client running time:" + oc::Timer + "Comm: X MB"     (party 0)
    #   "party t running time:" + oc::Timer + "Comm: X MB"    (party n-t-1)
    #   "last party running time:" + oc::Timer + "Comm: X MB" (party n-1)
    # Timer "end" line gives total time in ms.
    # Total time = last party's time (it computes intersection, finishes last).

    # Helper: extract time from oc::Timer "end" line following a role header
    extract_paxos_time() {
        local role_pattern="$1"
        awk -v pat="$role_pattern" '
            $0 ~ pat { found = 1 }
            found && /^end/ {
                for (i = 2; i <= NF; i++) {
                    if ($i ~ /^[0-9.]+$/) { printf "%.3f", $i / 1000; exit }
                }
            }
        ' "$log_file" 2>/dev/null
    }

    # Helper: extract Comm (MB) following a role header
    extract_paxos_comm() {
        local role_pattern="$1"
        awk -v pat="$role_pattern" '
            $0 ~ pat { found = 1 }
            found && /^Comm:/ {
                for (i = 1; i <= NF; i++) {
                    if ($i ~ /^[0-9.]+$/) { print $i; exit }
                }
            }
        ' "$log_file" 2>/dev/null
    }

    local client_time_s client_comm_mb leader_time_s leader_comm_mb last_time_s last_comm_mb
    client_time_s=$(extract_paxos_time "Client running time")
    client_comm_mb=$(extract_paxos_comm "Client running time")
    leader_time_s=$(extract_paxos_time "party t running time")
    leader_comm_mb=$(extract_paxos_comm "party t running time")
    last_time_s=$(extract_paxos_time "last party running time")
    last_comm_mb=$(extract_paxos_comm "last party running time")

    if [ -z "$client_time_s" ] && [ -z "$last_time_s" ]; then
        echo "$setting,mpsi-paxos,2^$nn,$n,$t,FAIL,N/A,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL" >> "$OUTPUT"
        return
    fi

    # Convert MB to KB
    local client_comm_kb="" leader_comm_kb="" last_comm_kb="" total_comm_kb=""
    [ -n "$client_comm_mb" ] && client_comm_kb=$(awk "BEGIN { printf \"%.2f\", $client_comm_mb * 1024 }")
    [ -n "$leader_comm_mb" ] && leader_comm_kb=$(awk "BEGIN { printf \"%.2f\", $leader_comm_mb * 1024 }")
    [ -n "$last_comm_mb" ] && last_comm_kb=$(awk "BEGIN { printf \"%.2f\", $last_comm_mb * 1024 }")
    total_comm_kb=$(awk "BEGIN { printf \"%.2f\", ${client_comm_mb:-0} * 1024 + ${leader_comm_mb:-0} * 1024 + ${last_comm_mb:-0} * 1024 }")

    # Map: Receiver=last party (n-1), Leader=party-t, Sender=client (party 0)
    # Total time = last party time (computes intersection)
    local total_time_s="${last_time_s:-$client_time_s}"

    echo "$setting,mpsi-paxos,2^$nn,$n,$t,${last_time_s:-N/A},N/A,${last_comm_kb:-N/A},${leader_time_s:-N/A},${leader_comm_kb:-N/A},${client_time_s:-N/A},N/A,${client_comm_kb:-N/A},$total_time_s,${total_comm_kb:-N/A}" >> "$OUTPUT"
}

parse_multipartypsi() {
    local log_file="$1"
    local setting="$2"
    local nn="$3"
    local n="$4"
    local t="$5"

    # MultipartyPSI (CCS 2017) logs role blocks such as:
    #   "Client Idx: X" ... "Total time: Y s" ... "Comm: Z MB"
    #   "Leader Idx: X" ... "Total time: Y s" ... "Comm/Total data: Z MB"
    # Customer-requested mapping:
    #   Client block  -> single-user/client time
    #   Leader block  -> total time

    local parsed
    parsed=$(awk '
        /Client Idx:/ { role = "client" }
        /Leader Idx:/ { role = "leader" }

        /Total time:/ {
            value = ""
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^[0-9.]+$/) value = $i
            }
            if (value != "") {
                if (role == "client") {
                    if (value + 0 > client_time + 0) client_time = value
                } else if (role == "leader") {
                    if (value + 0 > leader_time + 0) leader_time = value
                } else if (value + 0 > fallback_time + 0) {
                    fallback_time = value
                }
            }
        }

        /^Comm:/ || /Total data:/ || /Total Comm:/ {
            total_mb_this_line = 0
            found = 0
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^[0-9.]+$/) {
                    total_mb_this_line += $i
                    found = 1
                }
            }
            if (found) {
                total_comm += total_mb_this_line
                if (role == "client" && total_mb_this_line > client_comm + 0)
                    client_comm = total_mb_this_line
                else if (role == "leader" && total_mb_this_line > leader_comm + 0)
                    leader_comm = total_mb_this_line
                else if (total_mb_this_line > fallback_comm + 0)
                    fallback_comm = total_mb_this_line
            }
        }

        END {
            printf "%s|%s|%s|%s|%s|%s|%s\n",
                   client_time, leader_time, client_comm, leader_comm,
                   total_comm, fallback_time, fallback_comm
        }
    ' "$log_file" 2>/dev/null)

    local client_time_s leader_time_s client_comm_mb leader_comm_mb total_comm_mb fallback_time_s fallback_comm_mb
    IFS='|' read -r client_time_s leader_time_s client_comm_mb leader_comm_mb total_comm_mb fallback_time_s fallback_comm_mb <<< "$parsed"

    if [ -z "$client_time_s" ] && [ -z "$leader_time_s" ] && [ -z "$fallback_time_s" ]; then
        echo "$setting,multipartypsi,2^$nn,$n,$t,FAIL,N/A,FAIL,N/A,N/A,N/A,N/A,N/A,FAIL,FAIL" >> "$OUTPUT"
        return
    fi

    if [ -z "$client_time_s" ]; then
        client_time_s="$fallback_time_s"
    fi
    if [ -z "$leader_time_s" ]; then
        leader_time_s="$fallback_time_s"
    fi
    if [ -z "$client_comm_mb" ]; then
        client_comm_mb="$fallback_comm_mb"
    fi
    if [ -z "$leader_comm_mb" ]; then
        leader_comm_mb="$fallback_comm_mb"
    fi

    local client_comm_kb="N/A" leader_comm_kb="N/A" total_comm_kb="N/A"
    [ -n "$client_comm_mb" ] && client_comm_kb=$(awk "BEGIN { printf \"%.2f\", $client_comm_mb * 1024 }")
    [ -n "$leader_comm_mb" ] && leader_comm_kb=$(awk "BEGIN { printf \"%.2f\", $leader_comm_mb * 1024 }")
    [ -n "$total_comm_mb" ] && total_comm_kb=$(awk "BEGIN { printf \"%.2f\", $total_comm_mb * 1024 }")

    # MultipartyPSI baseline only exposes client/leader roles in the logs:
    #   Leader_Time = leader block total
    #   Max_Sender_Time = client block total
    #   Total_Time = leader block total (customer-requested total)
    echo "$setting,multipartypsi,2^$nn,$n,$t,N/A,N/A,N/A,${leader_time_s:-N/A},${leader_comm_kb:-N/A},${client_time_s:-N/A},N/A,${client_comm_kb:-N/A},${leader_time_s:-$client_time_s},${total_comm_kb:-N/A}" >> "$OUTPUT"
}

# Process all log files
COUNT=0
for log_file in "$LOG_DIR"/*.log; do
    [ -f "$log_file" ] || continue

    # Parse filename: {net}_{proto}_n{n}_t{t}_m{nn}.log
    basename=$(basename "$log_file" .log)

    # Extract fields
    net=$(echo "$basename" | cut -d_ -f1)
    # Proto may contain hyphens (e.g., oprf-hh, oring-ring)
    # Format: net_proto_nN_tT_mNN
    # We extract n, t, nn from the end, proto is everything between
    n_field=$(echo "$basename" | sed -nE 's/.*_(n[0-9]+)_t[0-9]+_m[0-9]+$/\1/p')
    t_field=$(echo "$basename" | sed -nE 's/.*_n[0-9]+_(t[0-9]+)_m[0-9]+$/\1/p')
    nn_field=$(echo "$basename" | sed -nE 's/.*_n[0-9]+_t[0-9]+_(m[0-9]+)$/\1/p')

    if [ -z "$n_field" ] || [ -z "$t_field" ] || [ -z "$nn_field" ]; then
        echo "[SKIP] Cannot parse filename: $basename"
        continue
    fi

    n="${n_field#n}"
    t="${t_field#t}"
    nn="${nn_field#m}"

    # Extract proto: between first _ and _n<digits>_t
    proto=$(echo "$basename" | sed "s/^${net}_//" | sed "s/_n${n}_t${t}_m${nn}$//")

    COUNT=$((COUNT + 1))

    case "$proto" in
        oprf|bc|ring|oprf-hh|bc-hh|ring-hh)
            parse_our_protocol "$log_file" "$net" "$proto" "$nn" "$n" "$t"
            ;;
        bzs)
            parse_bzs "$log_file" "$net" "$nn" "$n" "$t"
            ;;
        oring-ring|oring-star)
            parse_oring "$log_file" "$net" "$proto" "$nn" "$n" "$t"
            ;;
        mpsi-paxos)
            parse_mpsi_paxos "$log_file" "$net" "$nn" "$n" "$t"
            ;;
        multipartypsi)
            parse_multipartypsi "$log_file" "$net" "$nn" "$n" "$t"
            ;;
        *)
            echo "[SKIP] Unknown protocol in filename: $proto"
            ;;
    esac
done

echo ""
echo "Parsed $COUNT log files -> $OUTPUT"
echo ""

# Print summary table
if command -v column &>/dev/null; then
    column -t -s, "$OUTPUT" | head -20
    TOTAL_LINES=$(wc -l < "$OUTPUT")
    if [ "$TOTAL_LINES" -gt 21 ]; then
        echo "... ($((TOTAL_LINES - 1)) total rows)"
    fi
else
    head -20 "$OUTPUT"
fi
