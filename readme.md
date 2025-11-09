sudo mv stats_logger /usr/local/bin/
sudo chmod +x /usr/local/bin/stats_logger

sudo systemctl daemon-reload
sudo systemctl restart stats_logger

g++ stats_logger.cpp -O2 -lmbedtls -lmbedx509 -lmbedcrypto -o stats_logger
