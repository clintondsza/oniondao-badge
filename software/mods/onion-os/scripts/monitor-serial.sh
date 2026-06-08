#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BAUD="${BAUD:-115200}"
PORT="${PORT:-}"
BOARD_FILTER="${BOARD:-esp32s3}"
LIST_ONLY=0
SELECTED_CHIP=""
MONITOR_PID=""

usage() {
  cat <<'EOF'
Usage: scripts/monitor-serial.sh [options]

Search connected serial ports, detect ESP board type, prompt for a port, and
open the ESP-IDF serial monitor.

Options:
  -p, --port PORT       Monitor a specific serial port instead of selecting
  -b, --baud BAUD       Serial monitor baud rate (default: 115200, or $BAUD)
  --board BOARD         Board/chip filter: esp32s3, esp32, esp32c3, esp32c6, any
                        (default: esp32s3, or $BOARD)
  --list                List matching serial ports and exit
  -h, --help            Show this help

Environment:
  IDF_EXPORT=/path/to/export.sh  ESP-IDF export script to source
  PORT=/dev/cu.usbserial-210     Same as --port
  BAUD=115200                    Same as --baud
  BOARD=any                      Same as --board
EOF
}

normalize_board() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr -d '_ -'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      PORT="$2"
      shift 2
      ;;
    -b|--baud)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      BAUD="$2"
      shift 2
      ;;
    --board)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      BOARD_FILTER="$(normalize_board "$2")"
      shift 2
      ;;
    --list)
      LIST_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$BOARD_FILTER" in
  esp32s3|esp32|esp32c3|esp32c6|any) ;;
  *)
    echo "Unsupported board filter: $BOARD_FILTER" >&2
    echo "Use one of: esp32s3, esp32, esp32c3, esp32c6, any" >&2
    exit 2
    ;;
esac

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Required command not found: $1" >&2
    echo "Install ESP-IDF or pass IDF_EXPORT=/path/to/esp-idf/export.sh" >&2
    exit 1
  fi
}

source_esp_idf() {
  command -v idf.py >/dev/null 2>&1 && return 0

  local candidates=()
  if [[ -n "${IDF_EXPORT:-}" ]]; then
    candidates+=("$IDF_EXPORT")
  fi

  candidates+=(
    "$HOME/.espressif/v5.5.4/esp-idf/export.sh"
    "$HOME/.espressif-5.5.4/v5.5.4/esp-idf/export.sh"
    "$HOME/esp/esp-idf/export.sh"
    "$HOME"/.espressif/v5.5.*/esp-idf/export.sh
    "$HOME"/.espressif-5.5.*/v5.5.*/esp-idf/export.sh
    "$HOME/.platformio/packages/framework-espidf/export.sh"
  )

  local export_script
  local export_log="${TMPDIR:-/tmp}/onion-os-idf-export.log"
  for export_script in "${candidates[@]}"; do
    if [[ -f "$export_script" ]]; then
      echo "Loading ESP-IDF from $export_script"
      # shellcheck disable=SC1090
      if . "$export_script" >"$export_log" 2>&1; then
        command -v idf.py >/dev/null 2>&1 && return 0
      else
        echo "Failed to load ESP-IDF from $export_script" >&2
        tail -n 40 "$export_log" >&2 || true
      fi
    fi
  done

  return 1
}

esptool_command() {
  if command -v esptool.py >/dev/null 2>&1; then
    echo "esptool.py"
  elif command -v esptool >/dev/null 2>&1; then
    echo "esptool"
  else
    echo ""
  fi
}

candidate_ports() {
  local patterns=(
    "/dev/cu.usbserial"* "/dev/tty.usbserial"*
    "/dev/cu.usbmodem"* "/dev/tty.usbmodem"*
    "/dev/cu.wchusbserial"* "/dev/tty.wchusbserial"*
    "/dev/cu.SLAB_USBtoUART"* "/dev/tty.SLAB_USBtoUART"*
    "/dev/ttyUSB"* "/dev/ttyACM"*
  )
  local port
  for port in "${patterns[@]}"; do
    case "$port" in
      /dev/tty.*)
        [[ -e "/dev/cu.${port#/dev/tty.}" ]] && continue
        ;;
    esac
    [[ -e "$port" ]] && printf '%s\n' "$port"
  done | sort -u
}

detect_chip() {
  local port="$1"
  local output chip attempt

  [[ -n "${ESPTOOL:-}" ]] || return 1

  for attempt in 1 2; do
    if output="$("$ESPTOOL" --chip auto --port "$port" chip_id 2>&1)"; then
      break
    fi

    if grep -Eiq 'busy|Resource temporarily unavailable|exclusively lock' <<<"$output"; then
      if [[ "$attempt" -eq 1 ]]; then
        sleep 0.5
        continue
      fi
      printf 'busy\n'
      return 2
    fi

    return 1
  done

  chip="$(awk '
    /Detecting chip type\.\.\./ { print $NF; exit }
    /^Chip is / { print $3; exit }
  ' <<<"$output")"

  [[ -n "$chip" ]] || return 1
  printf '%s\n' "$chip"
}

board_matches() {
  local chip="$1"
  local normalized_chip

  [[ "$BOARD_FILTER" == "any" ]] && return 0
  normalized_chip="$(normalize_board "$chip")"
  [[ "$normalized_chip" == "$BOARD_FILTER" ]]
}

scan_ports() {
  local include_unknown="$1"
  local port chip status

  PORTS=()
  CHIPS=()

  while IFS= read -r port; do
    [[ -n "$port" ]] || continue
    echo "Probing $port..." >&2
    if chip="$(detect_chip "$port")"; then
      status=0
    else
      status=$?
    fi
    if [[ "$status" -eq 0 ]]; then
      if board_matches "$chip"; then
        PORTS+=("$port")
        CHIPS+=("$chip")
      fi
    elif [[ "$include_unknown" -eq 1 ]]; then
      PORTS+=("$port")
      CHIPS+=("${chip:-unknown}")
    fi
  done < <(candidate_ports)
}

print_ports() {
  local i count
  count="${#PORTS[@]}"

  for ((i = 0; i < count; i++)); do
    printf '%2d) %-28s %s\n' "$((i + 1))" "${PORTS[$i]}" "${CHIPS[$i]}"
  done
}

select_port() {
  local count choice
  count="${#PORTS[@]}"

  if [[ "$count" -eq 0 ]]; then
    return 1
  fi

  if [[ ! -t 0 ]]; then
    PORT="${PORTS[0]}"
    SELECTED_CHIP="${CHIPS[0]}"
    return 0
  fi

  echo "Detected serial ports:"
  print_ports

  while true; do
    read -r -p "Select port [1-$count]: " choice
    if [[ -z "$choice" && "$count" -eq 1 ]]; then
      choice=1
    fi
    if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= count )); then
      PORT="${PORTS[$((choice - 1))]}"
      SELECTED_CHIP="${CHIPS[$((choice - 1))]}"
      return 0
    fi
    echo "Enter a number from 1 to $count." >&2
  done
}

kill_process_tree() {
  local pid="$1"
  local signal="$2"
  local child

  while IFS= read -r child; do
    [[ -n "$child" ]] || continue
    kill_process_tree "$child" "$signal"
  done < <(pgrep -P "$pid" 2>/dev/null || true)

  kill "-$signal" "$pid" 2>/dev/null || true
}

stop_monitor() {
  local signal="${1:-TERM}"

  [[ -n "$MONITOR_PID" ]] || return 0
  kill -0 "$MONITOR_PID" 2>/dev/null || return 0

  echo "Stopping serial monitor..." >&2
  kill_process_tree "$MONITOR_PID" "$signal"
  sleep 0.3

  if kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill_process_tree "$MONITOR_PID" TERM
    sleep 0.3
  fi

  if kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill_process_tree "$MONITOR_PID" KILL
  fi

  wait "$MONITOR_PID" 2>/dev/null || true
  MONITOR_PID=""
}

handle_interrupt() {
  echo >&2
  stop_monitor INT
  exit 130
}

handle_terminate() {
  stop_monitor TERM
  exit 143
}

source_esp_idf || true
require_command idf.py
ESPTOOL="$(esptool_command)"
if [[ -z "$ESPTOOL" ]]; then
  echo "Warning: esptool.py/esptool not found; board type detection is unavailable." >&2
fi

cd "$PROJECT_DIR"

if [[ -n "$PORT" ]]; then
  if [[ ! -e "$PORT" ]]; then
    echo "Serial port does not exist: $PORT" >&2
    exit 1
  fi

  CHIP="unknown"
  if ! CHIP="$(detect_chip "$PORT")"; then
    CHIP="${CHIP:-unknown}"
  fi
  SELECTED_CHIP="$CHIP"
  echo "Using $PORT ($CHIP)"
elif [[ "$BOARD_FILTER" == "any" ]]; then
  scan_ports 1
else
  scan_ports 0
  if [[ "${#PORTS[@]}" -eq 0 ]]; then
    echo "No $BOARD_FILTER boards were detected. Showing all serial ports for manual selection." >&2
    scan_ports 1
  fi
fi

if [[ -z "$PORT" ]]; then
  if [[ "${#PORTS[@]}" -eq 0 ]]; then
    echo "No serial ports found." >&2
    echo "Connect a badge, or pass a port explicitly with --port /dev/..." >&2
    exit 1
  fi

  if [[ "$LIST_ONLY" -eq 1 ]]; then
    print_ports
    exit 0
  fi

  select_port || {
    echo "No serial port selected." >&2
    exit 1
  }
fi

if [[ "$SELECTED_CHIP" == "busy" ]]; then
  echo "Serial port is busy: $PORT" >&2
  echo "Close the existing monitor or serial console, then run this script again." >&2
  exit 1
fi

echo "Opening serial monitor on $PORT at $BAUD baud..."
if [[ ! -t 0 || ! -r /dev/tty ]]; then
  echo "Serial monitor requires an interactive terminal." >&2
  exit 1
fi

trap handle_interrupt INT
trap handle_terminate TERM
trap 'stop_monitor TERM' EXIT

# Bash redirects stdin for background commands in non-job-control scripts.
# Reattach the monitor to the terminal so idf_monitor sees a TTY.
idf.py -p "$PORT" -b "$BAUD" monitor </dev/tty >/dev/tty 2>/dev/tty &
MONITOR_PID=$!

set +e
wait "$MONITOR_PID"
MONITOR_STATUS=$?
set -e

MONITOR_PID=""
trap - INT TERM EXIT
exit "$MONITOR_STATUS"
