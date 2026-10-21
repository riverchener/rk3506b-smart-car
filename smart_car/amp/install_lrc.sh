#!/bin/sh
# 把 /tmp/dengnixiake.lrc 装成 myMusic 里那首歌的同名歌词，并看 app 是否真的去读它
DST="/userdata/smart_car/myMusic/等你下课 (with 杨瑞代) - 周杰伦.lrc"
cp /tmp/dengnixiake.lrc "$DST"
chmod 666 "$DST"
echo "=== installed ==="
ls -la /userdata/smart_car/myMusic/
echo "=== head ==="
head -8 "$DST"
echo "=== watcher log (最近 10 行) ==="
tail -10 /tmp/lrc_watch.log
