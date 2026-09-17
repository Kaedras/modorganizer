#!/bin/sh

set -e

cd "$(dirname "$(realpath "$0")")"

exec ./ModOrganizer crashdump
