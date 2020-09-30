export DEBIAN_PKG=1
all: 
	$(MAKE) -k -C output/x86-xgs5-deb80

install:
	$(MAKE) -C output/x86-xgs5-deb80 install
	/usr/bin/install -d $(DESTDIR)/usr/include/sai
	/usr/bin/install -m 644 include/sai/*.h $(DESTDIR)/usr/include/sai

clean:
	$(MAKE) -k -C output/x86-xgs5-deb80 clean

.PHONY: all clean install
