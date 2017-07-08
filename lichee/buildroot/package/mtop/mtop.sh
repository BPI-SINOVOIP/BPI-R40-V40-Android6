#!/bin/sh
unset
version="0.5"

usage()
{
	printf "
Usage: %s [-n iter] [-d delay] [-m] [-o FILE] [-h]
       -n NUM   Updates to show before exiting.
       -d NUM   Seconds to wait between update.
       -m unit: MB
       -v Display mtop version.
       -h Display this help screen.
       \n" $(basename $0)

	exit 1;
}

total=0
max=0
idx=0
tmax=0;

#Arg for program
count=0;
second=1;
unit="KB";

while getopts :n:d:mo:vh OPTION;
do
	case $OPTION in
	n)
		count=$OPTARG;
		;;
	d)
		second=$OPTARG;
		;;
	m)
		unit="MB";
		;;
	v)
		echo "version: $version"
		exit 0;
		;;
	h)
		usage
		;;
	?)
		usage
		;;
	esac
done
shift $((OPTIND - 1))


path="/sys/class/hwmon/mbus_pmu/device";
if [ ! -d ${path} ]; then
	printf "Path \"$path\" not exist. Please check it\n";
	exit 1;
fi

#Entry the work directory
#We do not need 'totddr' file, because some platform
#haven't it.
cd $path
pmus=$(ls pmu_* | grep -v "totddr" 2>/dev/null);

pre_count="";
cur_count="";

transferunit()
{
	if [ $# -ne 1 ]; then
		echo "Bug: Can not transfer it";
		exit 1
	fi

	if [ $unit == "MB" ]; then
		echo $(awk "BEGIN{printf \"%.4f\", $1/1024}" 2>/dev/null)
	else
		echo $1
	fi
}

while [ $count -eq 0 ] || [ $idx -lt $count ]; do

	total=$(expr $total + $tmax);

	if [ -n "${pre_count}" ]; then
		clear

		idx=$(expr $idx + 1);
		printf "\n----------------Num: %llu, Unit: %s, Interval: %ss----------------\n" \
			$idx $unit $second
		printf "Total: %s,\t" $(transferunit $total)
		printf "Max: %s,\t" $(transferunit $max)
		printf "Average: %s %s/s\n" $(transferunit $(awk "BEGIN{printf \"%.4f\", \
			$total/($idx * $second)}" 2>/dev/null)) $unit
	fi

	delta=0;
	pre=0;
	cur=0;
	tmax=0;
	pre_count=${cur_count};
	cur_count="";

	for pmu in ${pmus};
	do
		cur=$(cat ${pmu} 2>/dev/null);
		cur_count="${cur_count}${cur_count:+ }${cur}"

		if [ "${pre_count}"x == "x" ]; then
			continue;
		else
			pre=${pre_count%% *};
			pre_count=${pre_count#* };
		fi

		delta=$(expr $cur - $pre);
		tmax=$(expr $tmax + $delta);

		if [ $tmax -gt $max ]; then
			max=$tmax;
		fi

		if [ $idx -ge 1 ]; then
			printf "%s\t: %10s %s/s\n" ${pmu#*pmu_} $(transferunit \
				$(awk "BEGIN{printf \"%.4f\", $delta/$second}" 2>/dev/null))  $unit
		fi
	done

	if [ $idx -ge 1 ]; then
		sleep $second;
	fi
done

exit 0;

