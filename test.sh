#!/usr/bin/env bash
cd ./cmake-build-debug/ || exit

for N in {1..50}
do
  ./Client localhost:1026 f.txt g.txt &
done
wait
