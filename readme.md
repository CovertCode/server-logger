sudo mv stats_logger /usr/local/bin/
sudo chmod +x /usr/local/bin/stats_logger

sudo systemctl daemon-reload
sudo systemctl restart stats_logger

g++ stats_logger.cpp -O2 -lmbedtls -lmbedx509 -lmbedcrypto -o stats_logger

wget https://cdn.statically.io/gh/CovertCode/server-logger/main/stats_logger

[Clear Stats](http://152.53.50.193:14782/clear-stats?api_key=Safehouse-Sedate-Gore9-Duly)

bash <(wget -qO- https://raw.githubusercontent.com/CovertCode/server-logger/refs/heads/main/install-stats-logger.sh)

sudo wget -O /usr/local/bin/stats_logger https://cdn.statically.io/gh/CovertCode/server-logger/main/stats_logger
sudo chmod +x /usr/local/bin/stats_logger
