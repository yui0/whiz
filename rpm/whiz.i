# This is a i file, used by initng parsed by install_service

service service/whiz {
	need = system/bootmisc;
	exec start = /opt/whiz/sbin/whizserver -syslog;
	exec stop = /opt/whiz/bin/whizkill;
}
