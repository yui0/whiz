#!/bin/sh

installdir=/opt/whiz

mkdir -p ${installdir}/{bin,sbin,dic}

# install whiz server
install -m 755 sbin/whizserver ${installdir}/sbin

# install whiz killer
install -m 755 bin/whizkill ${installdir}/bin

# install whiz dic management
install -m 755 bin/whizdic ${installdir}/bin
install -m 755 bin/whizcui ${installdir}/bin

# install dic
install -m 644 dic/whiz.dic ${installdir}/dic
install -m 644 dic/whiz.inx ${installdir}/dic
install -m 644 dic/connect.dic ${installdir}/dic
install -m 644 dic/connect.inx ${installdir}/dic
install -m 644 dic/forms.dic ${installdir}/dic
install -m 644 dic/grammar.dic ${installdir}/dic

# for whiz.init
install -m 755 whiz.init /etc/rc.d/init.d/whiz
