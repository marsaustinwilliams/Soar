#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
if [[ "${TRACE-0}" == "1" ]]; then
    set -o xtrace
fi

THISDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
export SOAR_HOME="$THISDIR"
export DYLD_LIBRARY_PATH="$SOAR_HOME"

FLAG=""
# SWT requirement: display must be created on main thread due to Cocoa restrictions
if [[ $(uname) == 'Darwin' ]]; then
  FLAG="-XstartOnFirstThread"
fi

REPO_ROOT=$( cd "$THISDIR/.." && pwd )
DEFAULT_SOURCE="$REPO_ROOT/UnitTests/SoarTestAgents/FunctionalTests_testTowersOfHanoiFast.soar"
SOURCE_FILE="${SOAR_DEBUGGER_SOURCE_FILE:-$DEFAULT_SOURCE}"

if [[ -f "$SOURCE_FILE" ]]; then
    java $FLAG -Djava.library.path="$SOAR_HOME" -jar "$SOAR_HOME/SoarJavaDebugger.jar" \
        -agent towersOfHanoiFast -source "$SOURCE_FILE" "$@" &
else
    echo "Warning: Towers of Hanoi Fast source file not found: $SOURCE_FILE" >&2
    echo "Set SOAR_DEBUGGER_SOURCE_FILE to override the startup source path." >&2
    java $FLAG -Djava.library.path="$SOAR_HOME" -jar "$SOAR_HOME/SoarJavaDebugger.jar" "$@" &
fi