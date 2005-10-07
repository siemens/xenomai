# arch2host.sh
# Returns a canonicalized host arch name from a shorthand arch token.
# $1 = Shorthand
# $2 = Default canonical value

case "$1" in
i?86)
    echo $1-pc-linux-gnu
    ;;
ppc|powerpc)
    echo powerpc-unknown-linux-gnu
    ;;
ppc64|powerpc64)
    echo powerpc64-unknown-linux-gnu
    ;;
ia64|itanium)
    echo ia64-unknown-linux-gnu
    ;;
"")
    # Shorthand not specified: return default value.
    echo $2
    ;;
*-*)
    # Not a shorthand: return "as is".
    echo $1
    ;;
esac
