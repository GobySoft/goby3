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

gen_authors()
{
    i=$1
    echo $i;
    mapfile -t authors < <(git blame --line-porcelain $i | grep "^author " | sort | uniq -c | sort -nr | sed 's/^ *//' | cut -d " " -f 3-)
    #    echo ${authors[@]}
    start_year=$(git log --reverse --date=format:%Y --format=format:%ad $i | head -n 1)
    end_year=$(git log -n 1 --date=format:%Y --format=format:%ad $i)
    #    echo ${start_year}-${end_year}    

    if [[ "${start_year}" == "${end_year}" ]]; then
        years="${start_year}"
    else
        years="${start_year}-${end_year}"
    fi
    
    cat <<EOF > /tmp/goby_authors.tmp
// Copyright ${years}:
EOF
    
    if (( $end_year >= 2013  )); then
        echo "//   GobySoft, LLC (2013-)" >>  /tmp/goby_authors.tmp
    fi
    if (( $start_year <= 2014 )); then
       echo "//   Massachusetts Institute of Technology (2007-2014)" >>  /tmp/goby_authors.tmp
    fi

    cat <<EOF >> /tmp/goby_authors.tmp
//   Community contributors (see AUTHORS file)
// File authors:
EOF
    
    for author in "${authors[@]}"
    do
        # use latest email for author name here
        email=$(git log --author "$author" -n 1 --format=format:%ae)
        if [ ! -z "$email" ]; then
            email="<${email}>"
        fi
        echo "//   $author $email"  >> /tmp/goby_authors.tmp
    done
}

pushd ../src
header_strip
for i in `find -regex ".*\.h$\|.*\.cpp$"`;
do
    gen_authors $i
    cat /tmp/goby_authors.tmp $here/../src/share/doc/header_lib.txt $i > $i.tmp; mv $i.tmp $i;
done
popd

for dir in ../src/apps ../src/test ../src/share/examples; do
    pushd $dir
    header_strip
    for i in `find -regex ".*\.h$\|.*\.cpp$"`;
    do
        gen_authors $i
        cat /tmp/goby_authors.tmp $here/../src/share/doc/header_bin.txt $i > $i.tmp; mv $i.tmp $i;
    done
    popd
done

rm /tmp/goby_authors.tmp

