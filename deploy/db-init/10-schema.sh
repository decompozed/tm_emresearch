#!/bin/sh
# Loads microvolts-db.sql on the first start of an empty database volume (the MariaDB image runs everything in
# /docker-entrypoint-initdb.d once, while its temporary server listens on the socket).
#
# Why not just mount the .sql file there: the dump has lost its header lines, the ones that save the session variables
# (@OLD_TIME_ZONE and others), but it still has the footer that restores them. Setting TIME_ZONE to NULL is an error and
# stops the whole import. So the variables are saved first, in the same session as the import.
{
    echo "SET @OLD_TIME_ZONE=@@TIME_ZONE, @OLD_SQL_MODE=@@SQL_MODE, @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT, @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS, @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION, @OLD_SQL_NOTES=@@SQL_NOTES;"
    cat /schema/microvolts-db.sql
} | MYSQL_PWD="$(cat /run/secrets/db_root_password)" mariadb --protocol=socket --socket=/run/mysqld/mysqld.sock -uroot microvolts-db
