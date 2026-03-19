echo AUDIT cat_multifile_begin
echo a > /home/user/a
echo b > /home/user/b
echo c > /home/user/c
/usr/bin/cat /home/user/a /home/user/b /home/user/c > /home/user/o
status=$?
echo AUDIT cat_multifile_status=$status
echo AUDIT cat_multifile_done
exit $status
