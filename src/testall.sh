effect="$*"
t() {
	format=$1
	shift
	opts="$*"

	echo "Format: $format   Options: $opts"
	./sox monkey.voc $opts /tmp/monkey.$format $effect
	./sox $opts /tmp/monkey.$format /tmp/monkey1.voc  $effect
}
t 8svx
t aiff
t au 
t avr
t cdr
t cvs
t dat
t vms
t hcom -r 22050
t maud
t raw -r 8130 -t ub
t sf 
t smp
t sndr
t sndt 
t txw
t voc
t wav 
t wve 
