#!/bin/sh -eu

echo " #!/bin/sh
./gen_terasort 2500000" > run.sh
chmod +x run.sh

rm -f input
touch input
for (( i = START ; i < START + 4000; i++ ))
do
    echo -e "$i\t\t" >> input
done

$MAPREDUCE -server $SERVER -write "$INPUT" <input
