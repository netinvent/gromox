#!/bin/sh

cd LIBRARY && make && cd ..
cd MTA_SYSTEM
make && make release && cd ..
cd MRA_SYSTEM
make && make release && cd ..
cd EXCHANGE_SYSTEM
make && make release && cd ..
cd AGENT_SERVICE
make && make release && cd ..
cd DOMAIN_ADMIN
make && make release && cd ..
cd SYSTEM_ADMIN
make && make release && cd ..
cd ARCHIVE_SYSTEM
make && make release && cd ..
