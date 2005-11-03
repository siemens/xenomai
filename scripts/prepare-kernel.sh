#! /bin/bash
set -e

usage='usage: prepare-kernel --linux=<linux-tree> --adeos=<adeos-patch> [--arch=<arch>]'
me=`basename $0`

while test $# -gt 0; do
    case "$1" in
    --linux=*)
	linux_tree=`echo $1|sed -e 's,--linux=\\(.*\\)$,\\1,g'`;
	linux_tree=`eval "echo $linux_tree"`
	;;
    --adeos=*)
	adeos_patch=`echo $1|sed -e 's,--adeos=\\(.*\\)$,\\1,g'`;
	adeos_patch=`eval "echo $adeos_patch"`
	;;
    --arch=*)
	linux_arch=`echo $1|sed -e 's,--arch=\\(.*\\)$,\\1,g'`;
	;;
    --verbose)
	verbose=1
	;;
    --help)
	echo "$usage"
	exit 0
	;;
    *)
	echo "$me: unknown flag: $1"
	echo "$usage"
	exit 1
	;;
    esac
    shift
done

# Infere the location of the Xenomai source tree from
# the path of the current script.

script_path=`type -p $0`
xenomai_root=`dirname $script_path`/..
xenomai_root=`cd $xenomai_root && pwd`

# Check the Linux tree

default_linux_tree=/lib/modules/`uname -r`/source

while test x$linux_tree = x; do
   echo -n "Linux tree [default $default_linux_tree]: "
   read linux_tree
   if test x$linux_tree = x; then
      linux_tree=$default_linux_tree
   fi
   if test \! -x "$linux_tree"; then
      echo "$me: cannot access Linux tree in $linux_tree"
      linux_tree=
   else
      linux_tree=`cd $linux_tree && pwd`
      break
   fi
done

if test \! -r $linux_tree/Makefile; then
   echo "$me: $linux_tree is not a valid Linux kernel tree"
   exit 2
fi

# Infere the default architecture if unspecified.

if test x$linux_arch = x; then
   build_arch=`$xenomai_root/config/config.guess`
   default_linux_arch=`echo $build_arch|cut -f1 -d-`
fi

while : ; do
   if test x$linux_arch = x; then
      echo -n "Target architecture [default $default_linux_arch]: "
      read linux_arch
      if test x$linux_arch = x; then
         linux_arch=$default_linux_arch
      fi
   fi
   case "$linux_arch" in
   i*86)
      linux_arch=i386
      xenomai_arch=i386
      ;;
   ppc|ppc32|powerpc)
      linux_arch=ppc
      xenomai_arch=powerpc
      ;;
   ppc64|powerpc64)
      linux_arch=ppc64
      xenomai_arch=powerpc
      ;;
   ia64)
      linux_arch=ia64
      xenomai_arch=ia64
      ;;
   *)
      echo "$me: unsupported architecture: $linux_arch"
      linux_arch=
      ;;
   esac
   if test \! x$linux_arch = x; then
      break
   fi
done

# Some kernel versions have merged 32/64 bit powperpc trees:
# canonicalize if needed.

if test "$xenomai_arch" = powerpc -a -d $linux_tree/arch/powerpc; then
   linux_arch=powerpc
fi

eval linux_`grep '^EXTRAVERSION =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^PATCHLEVEL =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^SUBLEVEL =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^VERSION =' $linux_tree/Makefile | sed -e 's, ,,g'`

linux_version="$linux_VERSION.$linux_PATCHLEVEL.$linux_SUBLEVEL$linux_EXTRAVERSION"

echo "Preparing kernel $linux_version in $linux_tree..."

if test -r $linux_tree/include/linux/ipipe.h \
     -o -r $linux_tree/include/linux/adeos.h; then
    echo "Adeos/$linux_arch patch already applied - bypassing patch."
else
   if test x$adeos_patch = x; then
      default_adeos_patch=`( ls $xenomai_root/ksrc/arch/$xenomai_arch/patches/adeos-ipipe-*|sort -r; \
		             ls $xenomai_root/ksrc/arch/$xenomai_arch/patches/adeos-linux-*|sort -r) | head -1`
   fi
   while test x$adeos_patch = x; do
      echo -n "Adeos patch [default $default_adeos_patch]: "
      read adeos_patch
      if test x$adeos_patch = x; then
         adeos_patch=$default_adeos_patch
      fi
      if test \! -r "$adeos_patch"; then
         echo "$me: cannot read Adeos patch from $adeos_patch"
         adeos_patch=
      fi
   done
   cat $adeos_patch | (cd $linux_tree && patch -p1 )
fi

if test -r $linux_tree/include/linux/ipipe.h; then
   adeos_version=`grep '^#define.*IPIPE_ARCH_STRING.*"' $linux_tree/include/asm-$linux_arch/ipipe.h|sed -e 's,.*"\(.*\)"$,\1,'`
   adeos_gen=newgen
elif test -r $linux_tree/include/linux/adeos.h; then
   adeos_version=`grep '^#define.*ADEOS_ARCH_STRING.*"' $linux_tree/include/asm-$linux_arch/adeos.h|sed -e 's,.*"\(.*\)"$,\1,'`
   adeos_gen=oldgen
fi
if test \! x$adeos_version = x; then
   echo "Adeos/$linux_arch $adeos_version ($adeos_gen) installed."
fi

# Do not use ln -sf here, we must first remove then possibly relink.

rm -f $linux_tree/arch/$linux_arch/xenomai
ln -s $xenomai_root/ksrc/arch/$xenomai_arch $linux_tree/arch/$linux_arch/xenomai
rm -f $linux_tree/include/asm-$linux_arch/xenomai
ln -s $xenomai_root/include/asm-$xenomai_arch $linux_tree/include/asm-$linux_arch/xenomai
rm -f $linux_tree/include/asm-generic/xenomai
ln -s $xenomai_root/include/asm-generic $linux_tree/include/asm-generic/xenomai
rm -f $linux_tree/include/xenomai
ln -s $xenomai_root/include $linux_tree/include/xenomai
rm -f $linux_tree/kernel/xenomai
ln -s $xenomai_root/ksrc $linux_tree/kernel/xenomai
rm -f $linux_tree/drivers/xenomai
ln -s $xenomai_root/ksrc/drivers $linux_tree/drivers/xenomai
echo 'Links installed.'

if ! grep -q XENOMAI $linux_tree/init/Kconfig; then
   sed -e "s,@LINUX_ARCH@,$linux_arch,g" $xenomai_root/scripts/Kconfig.frag >> $linux_tree/init/Kconfig
fi

if ! grep -q CONFIG_XENOMAI $linux_tree/arch/$linux_arch/Makefile; then
   p="drivers-\$(CONFIG_XENOMAI)		+= arch/$linux_arch/xenomai/"
   ( echo ; echo $p ) >> $linux_tree/arch/$linux_arch/Makefile
fi

if ! grep -q CONFIG_XENOMAI $linux_tree/drivers/Makefile; then
   p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
   ( echo ; echo $p ) >> $linux_tree/drivers/Makefile
fi

if ! grep -q CONFIG_XENOMAI $linux_tree/kernel/Makefile; then
   p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
   ( echo ; echo $p ) >> $linux_tree/kernel/Makefile
fi
echo 'Build system updated.'

echo Done.

exit 0
