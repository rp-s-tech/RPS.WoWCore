-- Core DB user for the local worldserver/bnetserver (run as root inside the container: make db-user).
-- '%' host: connections arrive through the Docker NAT; the port is bound to 127.0.0.1 only.
-- In DB_MODE = native db.ps1 replaces '%' with '127.0.0.1' (the server may listen on the LAN).
-- Placeholders are filled by dev/scripts/db.ps1. The site user (noble_site) is not touched.
CREATE USER IF NOT EXISTS '@DB_USER@'@'%' IDENTIFIED BY '@CORE_PASS@';
ALTER USER '@DB_USER@'@'%' IDENTIFIED BY '@CORE_PASS@';
GRANT ALL PRIVILEGES ON `@PREFIX@auth`.* TO '@DB_USER@'@'%';
GRANT ALL PRIVILEGES ON `@PREFIX@characters`.* TO '@DB_USER@'@'%';
GRANT ALL PRIVILEGES ON `@PREFIX@world`.* TO '@DB_USER@'@'%';
GRANT ALL PRIVILEGES ON `@PREFIX@hotfixes`.* TO '@DB_USER@'@'%';
GRANT ALL PRIVILEGES ON `@PREFIX@roleplay`.* TO '@DB_USER@'@'%';
FLUSH PRIVILEGES;
