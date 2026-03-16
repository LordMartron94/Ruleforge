#!/usr/bin/env bash
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"

# Always run from the script's directory (assumed to be repo root)
cd "$(dirname "$0")"

# --- Configuration ---
DEFAULT_PARENT_DIR="libs"
CONFIG_FILE="submodules.conf" # Default config file name

# --- ANSI Color Definitions ---
# Check if stdout is a terminal, otherwise output no color.
if tty -s; then
    # General
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    RED='\033[0;31m'
    CYAN='\033[0;36m'
    NC='\033[0m' # No Color

    # Styles
    BOLD='\033[1m'
else
    # Fallback to no colors if not running in a TTY
    GREEN=''
    YELLOW=''
    RED=''
    CYAN=''
    NC=''
    BOLD=''
fi

# Helper function to print colored output
log_success() {
    echo -e "${GREEN}${BOLD}✔ SUCCESS:${NC} ${1}"
}
log_warning() {
    echo -e "${YELLOW}${BOLD}⚠️ WARNING:${NC} ${1}" >&2
}
log_error() {
    echo -e "${RED}${BOLD}❌ ERROR:${NC} ${1}" >&2
}
log_critical() {
    echo -e "${RED}${BOLD}💥 CRITICAL ERROR:${NC} ${1}" >&2
}
log_info() {
    echo -e "${CYAN}${1}${NC}"
}
# --- End ANSI Color Definitions ---


# Function to ensure .gitmodules file exists
ensure_gitmodules() {
    if [ ! -f .gitmodules ]; then
        log_info "--> No .gitmodules file found, creating one..."
        touch .gitmodules
    fi
}

# Core function to add a single submodule
add_submodule() {
    local NAME="$1"
    local URL="$2"
    local PARENT_DIR="$3"
    local BATCH_MODE="${4:-false}"  # Optional 4th parameter for batch mode
    local LOCATION="$PARENT_DIR/$NAME"

    echo
    echo -e "${CYAN}--- Processing Submodule: ${BOLD}$NAME${NC}${CYAN} ---${NC}"
    echo "  URL:      $URL"
    echo "  Location: $LOCATION"

    # --- Idempotency Check ---
    if [ -d "$LOCATION/.git" ] || git config --get submodule."$NAME".url >/dev/null 2>&1; then
        log_info "  --> Submodule $NAME already exists (or location '$LOCATION' is a git repo)."
        
        if ! git submodule sync "$NAME" 2>/dev/null; then
           log_info "  Note: Submodule $NAME needed initialization/sync before update. (Benign)."
        fi
        
        # In batch mode, don't update existing submodules to preserve current branch
        if [ "$BATCH_MODE" = "true" ]; then
            log_info "      Batch mode: Skipping update to preserve current branch."
            log_success "Sync successful. Skipping 'git submodule add' and update."
            return 0
        else
            log_info "      Attempting to synchronize and update the existing submodule."
            if git submodule update --init --recursive "$LOCATION"; then
                log_success "Update successful. Skipping 'git submodule add'."
                return 0
            else
                log_error "Failed to update existing submodule '$NAME'. Manual intervention required."
                return 1
            fi
        fi
    fi
    # --- End Idempotency Check ---

    # Ensure parent directory exists
    mkdir -p "$PARENT_DIR"

    # Add the submodule (Only runs if the checks above passed)
    if git submodule add --name "$NAME" "$URL" "$LOCATION"; then
        log_success "Submodule added successfully."
        # Update/Init is critical here for new submodules (even in batch mode)
        if git submodule update --init --recursive "$LOCATION"; then
            log_info "Initialization and update complete."
        else
            # This is less common but indicates a failure in checkout after a successful add
            log_warning "Submodule added, but failed to initialize/checkout contents."
        fi
    else
        # This branch handles failures like bad URL, network issues, or "fatal: 'libs/memarch' does not have a commit checked out"
        # which you observed, indicating a deep checkout error.
        log_critical "Failed to add submodule '$NAME'. Check URL and permissions."
        return 1
    fi
}

# Function for interactive mode
interactive_mode() {
    echo -e "${CYAN}=== Git Submodule Manager (Interactive Mode) ===${NC}"
    echo
    echo "Choose an action:"
    echo "  [1] Add submodule"
    echo "  [2] Remove submodule"
    echo
    read -rp "Selection: " ACTION

    case "$ACTION" in
        1)
            read -rp "Enter submodule name (e.g. vision): " NAME
            read -rp "Enter repository URL: " URL
            read -rp "Enter parent directory (default: $DEFAULT_PARENT_DIR): " PARENT_DIR
            PARENT_DIR="${PARENT_DIR:-$DEFAULT_PARENT_DIR}"
            echo
            ensure_gitmodules
            add_submodule "$NAME" "$URL" "$PARENT_DIR" "false"
            ;;
        2)
            interactive_remove_mode
            ;;
        *)
            log_info "No action selected."
            ;;
    esac
}

# Function for batch/config mode
process_config_file() {
    local CONFIG_PATH="$1"
    echo -e "${CYAN}=== Git Submodule Adder (Batch Mode) ===${NC}"
    log_info "Reading configuration from: ${BOLD}$CONFIG_PATH${NC}"
    
    if [ ! -f "$CONFIG_PATH" ]; then
        log_error "Configuration file not found at '$CONFIG_PATH'."
        exit 1
    fi

    ensure_gitmodules
    
    # Read the file line by line
    local LINE_NUM=0
    while IFS=$' \t\n' read -r LINE || [ -n "$LINE" ]; do
        LINE_NUM=$((LINE_NUM + 1))
        if [[ "$LINE" =~ ^[[:space:]]*# ]] || [[ -z "$LINE" ]]; then
            continue
        fi

        local FIELDS
        FIELDS=($LINE)
        
        local NAME="${FIELDS[0]}"
        local URL="${FIELDS[1]}"
        local PARENT_DIR="${FIELDS[2]:-$DEFAULT_PARENT_DIR}" # Use default if not provided

        # Minimal validation
        if [ -z "$NAME" ] || [ -z "$URL" ]; then
            log_warning "Skipping line $LINE_NUM. Invalid format. Expected: NAME URL [PARENT_DIR]"
            continue
        fi

        # We call the function and capture the exit status.
        # || true is used in the main loop to ensure processing continues even if one submodule fails.
        # Pass "true" as 4th argument to indicate batch mode
        add_submodule "$NAME" "$URL" "$PARENT_DIR" "true" || true 
    done < "$CONFIG_PATH"
    
    echo
    log_info "Batch setup complete! All configured submodules processed."
}

list_submodules() {
    git config --file .gitmodules --get-regexp '^submodule\..*\.path$' \
    | while read -r key path; do
        name="${key#submodule.}"
        name="${name%.path}"
        echo "$name|$path"
    done
}

remove_submodule() {
    local NAME="$1"
    local PATH="$2"

    echo
    log_warning "You are about to REMOVE submodule '$NAME'"
    echo "  Path: $PATH"
    read -rp "Type the submodule name to confirm: " CONFIRM

    [ "$CONFIRM" = "$NAME" ] || { log_info "Aborted."; return; }

    # 1. Deinit
    git submodule deinit -f -- "$PATH" || true

    # 2. Remove from index + .gitmodules
    git rm -f "$PATH"

    # 3. Final cleanup (idempotent)
    /bin/rm -rf ".git/modules/$PATH"

    log_success "Submodule '$NAME' fully removed."
    log_info "Commit the changes when ready."
}


interactive_remove_mode() {
    echo -e "${CYAN}=== Git Submodule Remover (Interactive Mode) ===${NC}"

    local ENTRIES
    mapfile -t ENTRIES < <(list_submodules)

    if [ "${#ENTRIES[@]}" -eq 0 ]; then
        log_info "No submodules found."
        return
    fi

    echo
    echo "Available submodules:"
    local i=1
    for entry in "${ENTRIES[@]}"; do
        IFS='|' read -r name path <<< "$entry"
        echo "  [$i] $name  →  $path"
        i=$((i + 1))
    done

    echo
    read -rp "Select submodule number to remove (or empty to cancel): " IDX

    if [ -z "$IDX" ]; then
        log_info "Cancelled."
        return
    fi

    if ! [[ "$IDX" =~ ^[0-9]+$ ]] || [ "$IDX" -lt 1 ] || [ "$IDX" -gt "${#ENTRIES[@]}" ]; then
        log_error "Invalid selection."
        return 1
    fi

    IFS='|' read -r NAME PATH <<< "${ENTRIES[$((IDX - 1))]}"
    remove_submodule "$NAME" "$PATH"
}

# --- Main Execution Flow ---

# Check if a configuration file path is provided as an argument
if [ "$#" -gt 0 ]; then
    # Argument provided, use it as the config file path
    process_config_file "$1"
else
    # No argument provided, run in interactive mode
    interactive_mode
fi