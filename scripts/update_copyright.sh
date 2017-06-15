#!/bin/bash

here=`pwd`

header_strip()
{
    endtext='If not, see <http:\/\/www.gnu.org\/licenses\/>.'
    for i in `grep -lr "$endtext" | egrep "\.cpp$|\.h$"`
    do 
        echo $i
        l=$(grep -n "$endtext" $i | tail -1 | cut -d ":" -f 1)
        l=$(($l+1))
        echo $l
        tail -n +$l $i | sed '/./,$!d' > $i.tmp;
        mv $i.tmp $i
    done
}

pushd ../src
header_strip
for i in `find -regex ".*\.h$\|.*\.cpp$"`; do cat $here/../src/share/doc/header_lib.txt $i > $i.tmp; mv $i.tmp $i; done
popd

for dir in ../src/apps ../src/test ../src/share/examples; do
pushd $dir
header_strip
for i in `find -regex ".*\.h$\|.*\.cpp$"`; do cat $here/../src/share/doc/header_bin.txt $i > $i.tmp; mv $i.tmp $i; done
popd

done
