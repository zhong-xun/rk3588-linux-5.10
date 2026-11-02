#!/bin/bash -e

SITE="${1:-www.baidu.com}"
SITE_NAME="${2:-$SITE}"
EXTRA_MSG="$3"

if [ "${TOPEET_HOST_OFFLINE:-0}" = 1 ]; then
	echo "Network check skipped: offline mode is enabled."
	exit 0
fi

URL="$SITE"
case "$URL" in
	*://*) ;;
	*) URL="http://$URL" ;;
esac
HOST="${URL#*://}"
HOST="${HOST%%/*}"

if command -v curl >/dev/null 2>&1; then
	case "$(curl -I -s --connect-timeout 2 --max-time 3 -w '%{http_code}' -o /dev/null "$URL")" in
		2* | 3*) exit 0 ;;
	esac
fi
if command -v ping >/dev/null 2>&1 && ping "$HOST" -c 1 -W 1 >/dev/null 2>&1; then
	exit 0
fi

echo "WARNING: Network is unavailable for $SITE_NAME; continuing without it."
[ -z "$EXTRA_MSG" ] || echo "$EXTRA_MSG"
if [ "${TOPEET_NETWORK_STRICT:-0}" = 1 ]; then
	echo "Set TOPEET_NETWORK_STRICT=0 to continue without network access."
	exit 1
fi
exit 0
