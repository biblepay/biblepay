#!/bin/bash

export LC_ALL=C
set -e

# Get Tor service IP if running
if [[ "$1" == "biblepayd" ]]; then
  # Because biblepayd only accept torcontrol= host as an ip only, we resolve it here and add to config
  if [[ "$TOR_CONTROL_HOST" ]] && [[ "$TOR_CONTROL_PORT" ]] && [[ "$TOR_PROXY_PORT" ]]; then
    TOR_IP=$(getent hosts $TOR_CONTROL_HOST | cut -d ' ' -f 1)
    echo "proxy=$TOR_IP:$TOR_PROXY_PORT" >> "$HOME/.biblepaycore/biblepay.conf"
    echo "Added "proxy=$TOR_IP:$TOR_PROXY_PORT" to $HOME/.biblepaycore/biblepay.conf"
    echo "torcontrol=$TOR_IP:$TOR_CONTROL_PORT" >> "$HOME/.biblepaycore/biblepay.conf"
    echo "Added "torcontrol=$TOR_IP:$TOR_CONTROL_PORT" to $HOME/.biblepaycore/biblepay.conf"
    echo -e "\n"
  else
    echo "Tor control credentials not provided"
  fi
fi

exec "$@"
