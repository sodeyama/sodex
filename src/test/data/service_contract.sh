. /etc/init.d/rc.common
echo AUDIT service_contract_begin
service sshd start
echo AUDIT service_start=$?
service sshd status
echo AUDIT service_status_running=$?
service sshd force-reload
echo AUDIT service_force_reload=$?
service sshd stop
echo AUDIT service_stop=$?
service sshd status
echo AUDIT service_status_stopped=$?
service sshd force-reload
echo AUDIT service_force_reload_stopped=$?
echo abc > /var/run/sshd.pid
service sshd status
echo AUDIT service_status_invalid=$?
rm /var/run/sshd.pid
mv /etc/sodex-admin.conf /etc/sodex-admin.conf.off
service sshd start
echo AUDIT service_start_missing_config=$?
mv /etc/sodex-admin.conf.off /etc/sodex-admin.conf
mv /usr/bin/sshd /usr/bin/sshd.off
service sshd start
echo AUDIT service_start_missing_binary=$?
mv /usr/bin/sshd.off /usr/bin/sshd
echo AUDIT service_contract_done
exit 0
