#! /bin/bash
set -e

patch_copytempfile() {
    file="$1"
    if ! test -f "$temp_tree/$file"; then
        subdir=`dirname "$file"`
        mkdir -p "$temp_tree/$subdir"
        cp "$linux_tree/$file" "$temp_tree/$file"
    fi
}

patch_append() {
    file="$1"
    if test "x$output_patch" = "x"; then
        realfile="$linux_tree/$file"
    else
        patch_copytempfile "$file"
        realfile="$temp_tree/$file"
    fi
    cat >> "$realfile"
}

patch_ed() {
    file="$1"
    if test "x$output_patch" = "x"; then
        realfile="$linux_tree/$file"
    else
        patch_copytempfile "$file"
        realfile="$temp_tree/$file"
    fi
    ed -s "$realfile" > /dev/null
}

patch_link() {
    recursive="$1"              # "r" or "n"
    link_makefiles="$2"         # "m" or "n"
    target_dir="$3"
    link_dir="$4"

    (
        recursive_opt=""
        directorytype_opt=""
        if test x$recursive = xr; then
            recursive_opts="-mindepth 1"
            directorytype_opt="-type d -o"
        else
            recursive_opt="-maxdepth 1"
        fi
        link_makefiles_opt=""
        if test x$link_makefiles = xm; then
            link_makefiles_opt="-name Makefile -o"
        fi

        if test "x$output_patch" = "x" -a -e $linux_tree/$link_dir; then
            cd $linux_tree/$link_dir &&
	    find . $recursive_opt \( $directorytype_opt \
                $link_makefiles_opt -name $config_file -o -name '*.[chS]' \) |
            while read f; do
                if test ! -e $xenomai_root/$target_dir/$f; then rm -Rf $f; fi
            done
        fi

        cd $xenomai_root/$target_dir &&
        find . $recursive_opt \
            \( $link_makefiles_opt -name $config_file -o -name '*.[chS]' \) |
        while read f; do
            f=`echo $f | cut -d/ -f2-`
            d=`dirname $f`
            if test "x$output_patch" = "x"; then
                mkdir -p $linux_tree/$link_dir/$d
                if test x$forcelink = x1 -o ! -h $linux_tree/$link_dir/$f; then
                    ln -sf $xenomai_root/$target_dir/$f $linux_tree/$link_dir/$f
                fi
            else
                mkdir -p $temp_tree/$link_dir/$d
                cp $xenomai_root/$target_dir/$f $temp_tree/$link_dir/$f
            fi
        done
    )

}

generate_patch() {
    (
    cd "$temp_tree"
    find . -type f |
    while read f; do
        diff -Naurd "$linux_tree/$f" "$f" |
        sed -e "s,^--- ${linux_tree}/\.\(/.*\)$,--- linux\1," \
            -e "s,^+++ \.\(/.*\)$,+++ linux-patched\1,"
    done
    )
}


usage='usage: prepare-kernel --linux=<linux-tree> --adeos=<adeos-patch> [--arch=<arch>] [--outpatch=<file> <tempdir>] [--forcelink]'
me=`basename $0`

while test $# -gt 0; do
    case "$1" in
    --linux=*)
	linux_tree=`echo $1|sed -e 's,^--linux=\\(.*\\)$,\\1,g'`
	linux_tree=`eval "echo $linux_tree"`
	;;
    --adeos=*)
	adeos_patch=`echo $1|sed -e 's,^--adeos=\\(.*\\)$,\\1,g'`
	adeos_patch=`eval "echo $adeos_patch"`
	;;
    --arch=*)
	linux_arch=`echo $1|sed -e 's,^--arch=\\(.*\\)$,\\1,g'`
	;;
    --outpatch=*)
	output_patch=`echo $1|sed -e 's,^--outpatch=\\(.*\\)$,\\1,g'`
	shift
	temp_tree=`echo $1|sed -e 's,^--tempdir=\\(.*\\)$,\\1,g'`
	;;
    --forcelink)
        forcelink=1
        ;;
    --verbose)
	verbose=1
	;;
    --help)
	echo "$usage"
	exit 0
	;;
    *)
	echo "$me: unknown flag: $1" >&2
	echo "$usage" >&2
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
      break
   fi
done

linux_tree=`cd $linux_tree && pwd`
linux_out=$linux_tree

if test \! -r $linux_tree/Makefile; then
   echo "$me: $linux_tree is not a valid Linux kernel tree"
   exit 2
fi

# Create an empty output patch file, and initialize the temporary tree.
if test "x$output_patch" != "x"; then

    if test ! -d $temp_tree; then
        echo "$me: $temp_tree (temporary tree) is not an existing directory" >&2
        exit 2
    fi
    temp_tree=`cd $temp_tree && pwd`

    patchdir=`dirname $output_patch`
    patchdir=`cd $patchdir && pwd`
    output_patch=$patchdir/`basename $output_patch`
    echo > "$output_patch"

fi

# Infer the default architecture if unspecified.

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
   x86|i*86)
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
   bfin|bfinnommu|blackfin)
      linux_arch=bfinnommu
      xenomai_arch=blackfin
      ;;
   arm)
      linux_arch=arm
      xenomai_arch=arm
      ;;
   *)
      echo "$me: unsupported architecture: $linux_arch" >&2
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

# Post-2005R3 RC3 blackfin kernels use "blackfin" instead of
# "bfinnommu": canonicalize if needed.

if test "$xenomai_arch" = blackfin -a -d $linux_tree/arch/blackfin; then
   linux_arch=blackfin
fi

foo=`grep '^KERNELSRC    := ' $linux_tree/Makefile | cut -d= -f2`
if [ ! -z $foo ] ; then
    linux_tree=$foo
fi
unset foo

eval linux_`grep '^EXTRAVERSION =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^PATCHLEVEL =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^SUBLEVEL =' $linux_tree/Makefile | sed -e 's, ,,g'`
eval linux_`grep '^VERSION =' $linux_tree/Makefile | sed -e 's, ,,g'`

linux_version="$linux_VERSION.$linux_PATCHLEVEL.$linux_SUBLEVEL$linux_EXTRAVERSION"

if test x$verbose = x1; then
echo "Preparing kernel $linux_version in $linux_tree..."
fi

if test -r $linux_tree/include/linux/ipipe.h; then
    if test x$verbose = x1; then
    echo "Adeos found - bypassing patch."
    fi
elif test -r $linux_tree/include/linux/adeos.h; then
   echo "$me: Deprecated Adeos (oldgen) support found in $linux_tree;" >&2
   echo "Upgrade required to Adeos/I-pipe (newgen)." >&2
   exit 2
else
   if test x$adeos_patch = x; then
      default_adeos_patch=`( ls $xenomai_root/ksrc/arch/$xenomai_arch/patches/adeos-ipipe-$linux_VERSION.$linux_PATCHLEVEL*|sort -r ) 2>/dev/null | head -1`
   fi
   if test x$default_adeos_patch = x; then
      default_adeos_patch=/dev/null
   fi
   while test x$adeos_patch = x; do
      echo -n "Adeos patch [default $default_adeos_patch]: "
      read adeos_patch
      if test x$adeos_patch = x; then
         adeos_patch=$default_adeos_patch
      fi
      if test \! -r "$adeos_patch"; then
         echo "$me: cannot read Adeos patch from $adeos_patch" >&2
         adeos_patch=
      fi
   done
   patchdir=`dirname $adeos_patch`; 
   patchdir=`cd $patchdir && pwd`
   adeos_patch=$patchdir/`basename $adeos_patch`
   curdir=$PWD
   cd $linux_tree && patch --dry-run -p1 -f < $adeos_patch || { 
        cd $curdir;
        echo "$me: Unable to patch kernel $linux_version with `basename $adeos_patch`." >&2
        exit 2;
   }
   patch -p1 -f -s < $adeos_patch
   cd $curdir
fi

adeos_version=`grep '^#define.*IPIPE_ARCH_STRING.*"' $linux_tree/include/asm-$linux_arch/ipipe.h|sed -e 's,.*"\(.*\)"$,\1,'`

if test \! x$adeos_version = x; then
   if test x$verbose = x1; then
   echo "Adeos/$linux_arch $adeos_version installed."
   fi
else
   echo "$me: $linux_tree has no Adeos support for $linux_arch" >&2
   exit 2
fi

case $linux_VERSION.$linux_PATCHLEVEL in

    #
    #  Linux v2.6 section
    #

    2.6)

    config_file=Kconfig

    if ! grep -q XENOMAI $linux_tree/init/Kconfig; then
	sed -e "s,@LINUX_ARCH@,$linux_arch,g" $xenomai_root/scripts/Kconfig.frag |
            patch_append init/Kconfig
    fi

    if ! grep -q CONFIG_XENOMAI $linux_tree/arch/$linux_arch/Makefile; then
	p="drivers-\$(CONFIG_XENOMAI)		+= arch/$linux_arch/xenomai/"
	( echo ; echo $p ) | patch_append arch/$linux_arch/Makefile
    fi

    if ! grep -q CONFIG_XENOMAI $linux_tree/drivers/Makefile; then
	p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
	( echo ; echo $p ) | patch_append drivers/Makefile
    fi

    if ! grep -q CONFIG_XENOMAI $linux_tree/kernel/Makefile; then
	p="obj-\$(CONFIG_XENOMAI)		+= xenomai/"
	( echo ; echo $p ) | patch_append kernel/Makefile
    fi
    ;;

    #
    #  Linux v2.4 section
    #

    2.4)

    export linux_arch
    config_file=Config.in

    if ! grep -q CONFIG_XENO $linux_tree/Makefile; then
	patch_ed Makefile <<EOF
/DRIVERS := \$(DRIVERS-y)
^r $xenomai_root/scripts/Modules.frag

.
wq
EOF
    fi
    for defconfig_file in .config arch/$linux_arch/defconfig; do
       if test -w $linux_tree/$defconfig_file; then
          if ! grep -q CONFIG_XENO $linux_tree/$defconfig_file; then
	      patch_ed $defconfig_file <<EOF
$
r $xenomai_root/scripts/defconfig.frag
.
wq
EOF
          fi
       fi
    done
    if ! grep -q CONFIG_XENO $linux_tree/arch/$linux_arch/Makefile; then
	patch_ed arch/$linux_arch/Makefile <<EOF
$
a

ifdef CONFIG_XENOMAI
SUBDIRS += arch/$linux_arch/xenomai
DRIVERS += arch/$linux_arch/xenomai/built-in.o
endif
.
wq
EOF
    fi
    if ! grep -q CONFIG_XENO $linux_tree/drivers/Makefile; then
	patch_ed drivers/Makefile <<EOF
/include \$(TOPDIR)\/Rules.make
i
mod-subdirs := xenomai
subdir-\$(CONFIG_XENOMAI) += xenomai

.
wq
EOF
    fi
    if ! grep -q CONFIG_XENO $linux_tree/kernel/Makefile; then
	patch_ed kernel/Makefile <<EOF
/include \$(TOPDIR)\/Rules.make
i
mod-subdirs := xenomai
subdir-\$(CONFIG_XENOMAI) += xenomai
obj-\$(CONFIG_XENOMAI) += xenomai/arch/generic/built-in.o

.
wq
EOF
    fi
    if ! grep -iq xenomai $linux_tree/arch/$linux_arch/config.in; then
	patch_ed arch/$linux_arch/config.in <<EOF
$
a

source arch/$linux_arch/xenomai/Config.in
.
wq
EOF
    fi
    ;;

    #
    #  Paranoid section
    #

    *)

    echo "$me: Unsupported kernel version $linux_VERSION.$linux_PATCHLEVEL.x" >&2
    exit 2
    ;;

esac

# Create local directories then symlink to the source files from
# there, so that we don't pollute the Xenomai source tree with
# compilation files.

patch_link r m ksrc/arch/$xenomai_arch arch/$linux_arch/xenomai
patch_link n m ksrc/ kernel/xenomai
patch_link n m ksrc/arch kernel/xenomai/arch
patch_link r m ksrc/arch/generic kernel/xenomai/arch/generic
patch_link r m ksrc/nucleus kernel/xenomai/nucleus
patch_link r m ksrc/skins kernel/xenomai/skins
patch_link r m ksrc/drivers drivers/xenomai
patch_link r n include/asm-$xenomai_arch include/asm-$linux_arch/xenomai
patch_link r n include/asm-generic include/asm-generic/xenomai
patch_link n n include include/xenomai
cd $xenomai_root
for d in include/* ; do
    if test -d $d -a -z "`echo $d | grep '^include/asm-'`"; then
        destdir=`echo $d | sed -e 's,^\(include\)\(/.*\)$,\1/xenomai\2,'`
        patch_link r n $d $destdir
    fi
done

if test "x$output_patch" != "x"; then
    if test x$verbose = x1; then
    echo 'Generating patch.'
    fi
    generate_patch > "$output_patch"
fi

if test x$verbose = x1; then
echo 'Links installed.'
echo 'Build system ready.'
fi

exit 0

