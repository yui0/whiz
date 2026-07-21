# ©2023 YUICHIRO NAKADA

define MAKECMD
g++ -o whizserver ./server/whizserver.cpp ./engine/libwhiz.a -Os -I.
g++ -o whizkill ./server/whizkill.c ./debug/debug.cpp -Os -I.
gcc -o gim.so immodule/_ng/im.c ./debug/debug.cpp ./jrkanji/jrkanji.cpp ./jrkanji/whiz_conv_table.cpp ./engine/libwhiz.a -I. -I./immodule/_ng -Os -shared -fPIC -DPIC -Wno-write-strings `pkg-config gtk+-3.0 --cflags --libs`
gcc -o gim2.so immodule/_ng/im.c ./debug/debug.cpp ./jrkanji/jrkanji.cpp ./jrkanji/whiz_conv_table.cpp ./engine/libwhiz.a -I. -I./immodule/_ng -Os -shared -fPIC -DPIC -Wno-write-strings `pkg-config gtk+-2.0 --cflags --libs`
endef
export MAKECMD

.PHONY: all
all::
	@echo "$${MAKECMD}" > /tmp/$$$$ ; $(SHELL) /tmp/$$$$ ; rm -f /tmp/$$$$

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(OBJS) _depend.inc

.PHONY: depend
depend: $(OBJS:.o=.c)
	-@ $(RM) _depend.inc
	-@ for i in $^; do cpp -MM $$i | sed "s/\ [_a-zA-Z0-9][_a-zA-Z0-9]*\.c//g" >> _depend.inc; done

-include _depend.inc
