#!/bin/sh
echo 10 > /home/user/numbers.txt
echo 2 >> /home/user/numbers.txt
echo 2 >> /home/user/numbers.txt
echo 1 >> /home/user/numbers.txt
sort -n /home/user/numbers.txt > /home/user/sort.txt
uniq -c /home/user/sort.txt > /home/user/uniq.txt
wc -l -w -c /home/user/numbers.txt > /home/user/wc.txt
head -n 2 /home/user/numbers.txt > /home/user/head.txt
tail -n 2 /home/user/numbers.txt > /home/user/tail.txt

echo foo > /home/user/grep.txt
echo bar >> /home/user/grep.txt
echo 'foo bar' >> /home/user/grep.txt
grep -n foo /home/user/grep.txt > /home/user/grep_out.txt

echo aa:bb:cc > /home/user/cut.txt
cut -d : -f 1,3 /home/user/cut.txt > /home/user/cut_out.txt

echo 'a   b' > /home/user/tr.txt
tr -s " " < /home/user/tr.txt > /home/user/tr_out.txt

echo foo > /home/user/sed.txt
echo keep >> /home/user/sed.txt
echo foofoo >> /home/user/sed.txt
sed -e 's/foo/bar/g' -e '2d' /home/user/sed.txt > /home/user/sed_out.txt

echo aa:bb:cc > /home/user/awk.txt
awk -F : '{ print $1, $3 }' /home/user/awk.txt > /home/user/awk_out.txt

echo AUDIT unix_text_tools_main_ready
/usr/bin/sh /etc/init.d/rcS.unix-text-extra
echo AUDIT unix_text_tools_done
