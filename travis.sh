#!/bin/bash

MODE="$1"
shift

function foo {
	for i in "$@"; do
		echo "> $i"
	done
}

function usage {
	echo "usage:"
	echo "    $0 createdb <dbname> [dbuser] [dbpassword]"
	echo "    $0 importdb <dbname> [dbuser] [dbpassword]"
	echo "    $0 build [configure args]"
	echo "    $0 test <dbname> [dbuser] [dbpassword]"
	echo "    $0 getplugins"
	exit 1
}

function aborterror {
	echo $@
	exit 1
}

case "$MODE" in
	createdb|importdb|test)
		DBNAME="$1"
		DBUSER="$2"
		DBPASS="$3"
		if [ -z "$DBNAME" ]; then
			usage
		fi
		if [ "$MODE" != "test" ]; then
			if [ -n "$DBUSER" ]; then
				DBUSER="-u $DBUSER"
			fi
			if [ -n "$DBPASS" ]; then
				DBPASS="-p$DBPASS"
			fi
		fi
		;;
esac

case "$MODE" in
	createdb)
		echo "Creating database $DBNAME..."
		mysql $DBUSER $DBPASS -e "create database $DBNAME;" || aborterror "Unable to create database."
		;;
	importdb)
		echo "Importing tables into $DBNAME..."
		mysql $DBUSER $DBPASS $DBNAME < sql-files/main.sql || aborterror "Unable to import main database."
		mysql $DBUSER $DBPASS $DBNAME < sql-files/logs.sql || aborterror "Unable to import logs database."
		;;
	build)
		(cd tools && ./validateinterfaces.py silent) || aborterror "Interface validation error."
		./configure $@ || aborterror "Configure error, aborting build."
		make sql -j3 || aborterror "Build failed."
		make plugin.script_mapquit -j3 || aborterror "Build failed."
		;;
	test)
		# FIXME: Not pretty. We need a way not to overwrite this, but rather override it.
		cat > conf/import/inter-server.conf << EOF
inter_configuration: {
	log: {
		sql_connection: {
			//default_codepage: ""
			//case_sensitive: false
			db_hostname: "localhost"
			db_username: "$DBUSER"
			db_password: "$DBPASS"
			db_database: "$DBNAME"
			//codepage:""
		}
	}
}
EOF
		[ $? -eq 0 ] || aborterror "Unable to import configuration, aborting tests."
		ARGS="--load-script npc/dev/test.txt "
		ARGS="--load-plugin script_mapquit $ARGS --load-script npc/dev/ci_test.txt"
		echo "Running Hercules with command line: ./map-server --run-once $ARGS"
		ASAN_OPTIONS=detect_leaks=0 ./map-server --run-once $ARGS 2>runlog.txt
		export errcode=$?
		export teststr=$(cat runlog.txt)
		if [[ -n "${teststr}" ]]; then
			echo "Sanitizer errors found."
			cat runlog.txt
			aborterror "Sanitize errors found."
		else
			echo "No sanitizer errors found."
		fi
		if [ ${errcode} -ne 0 ]; then
			echo "server terminated with exit code ${errcode}"
			aborterror "Test failed"
		fi
		;;
	getplugins)
		echo "Cloning plugins repository..."
		# Nothing to clone right now, all relevant plugins are part of the repository.
		#git clone http://github.com/HerculesWS/StaffPlugins.git || aborterror "Unable to fetch plugin repository"
		#if [ -f StaffPlugins/Haru/script_mapquit/script_mapquit.c -a -f StaffPlugins/Haru/script_mapquit/examples/ci_test.txt ]; then
		#	pushd src/plugins || aborterror "Unable to enter plugins directory."
		#	ln -s ../../StaffPlugins/Haru/script_mapquit/script_mapquit.c ./
		#	popd
		#else
		#	echo "Plugin not found, skipping advanced tests."
		#fi
		;;
	*)
		usage
		;;
esac
