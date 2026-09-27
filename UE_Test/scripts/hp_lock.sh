# Source me: hp_lock <owner-tag> acquires the shared HP lock (waits up to 40 min), hp_unlock releases it.
LOCK=/private/tmp/claude-501/osr-hp.lock
hp_lock() { local w=0; until mkdir "$LOCK" 2>/dev/null; do sleep 10; w=$((w+10)); [ $w -gt 2400 ] && { echo "lock busy: $(cat $LOCK/owner)"; return 1; }; done; echo "agent-ue $1 $(date +%T)" > "$LOCK/owner"; }
hp_unlock() { rm -rf "$LOCK"; }
