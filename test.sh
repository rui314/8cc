#!/bin/bash

function test {
  expected="$1"
  expr="$2"

  echo "$expr" | ./8cc > tmp.s
  if [ ! $? ]; then
    echo "Failed to compile $expr"
    exit
  fi
  gcc -o tmp.out driver.c tmp.s || exit
  result="`./tmp.out`"
  if [ "$result" != "$expected" ]; then
    echo "Test failed: $expected expected but got $result"
    exit
  fi
}

make -s 8cc

test 0 0
test 42 42

rm -f tmp.out tmp.s
echo "All tests passed"
