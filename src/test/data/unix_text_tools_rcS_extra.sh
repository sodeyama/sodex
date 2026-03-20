#!/bin/sh
echo one > /home/user/diff_left.txt
echo two >> /home/user/diff_left.txt
echo one > /home/user/diff_right.txt
echo three >> /home/user/diff_right.txt
diff -q /home/user/diff_left.txt /home/user/diff_right.txt > /home/user/diff_q.txt
diff -u /home/user/diff_left.txt /home/user/diff_right.txt > /home/user/diff_u.txt
tee /home/user/tee_file.txt < /home/user/diff_right.txt > /home/user/tee_stdout.txt
echo AUDIT unix_text_tools_after_diff

sort --numeric-sort --reverse --output=/home/user/sort_rev.txt /home/user/numbers.txt
echo AUDIT unix_text_tools_after_sort

echo a > /home/user/uniq_long.txt
echo a >> /home/user/uniq_long.txt
echo b >> /home/user/uniq_long.txt
echo c >> /home/user/uniq_long.txt
echo c >> /home/user/uniq_long.txt
uniq --count --repeated /home/user/uniq_long.txt > /home/user/uniq_repeated.txt
echo AUDIT unix_text_tools_after_uniq

echo 1 > /home/user/head_long.txt
echo 2 >> /home/user/head_long.txt
echo 3 >> /home/user/head_long.txt
head --lines=-1 /home/user/head_long.txt > /home/user/head_skip_last.txt
tail --lines=+2 /home/user/head_long.txt > /home/user/tail_from_second.txt
echo AUDIT unix_text_tools_after_head_tail

echo Needle > /home/user/grep_long.txt
echo other >> /home/user/grep_long.txt
echo NEEDLE >> /home/user/grep_long.txt
grep --fixed-strings --ignore-case --count needle - < /home/user/grep_long.txt > /home/user/grep_count.txt
echo AUDIT unix_text_tools_after_grep

find /etc/init.d/rcS -name 'rc*' > /home/user/grep.txt
find / -name 'rc*' > /home/user/numbers.txt
echo AUDIT unix_text_tools_after_find

echo AUDIT unix_text_tools_extra_done
