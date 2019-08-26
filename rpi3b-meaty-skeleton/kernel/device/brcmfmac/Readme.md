BCM43430 uses sdio interface then we need to use bcdc protocol, for pcie we need to use msgbuf protocol.

Reference https://git.congatec.com/android/qmx6_kernel/commit/f1d56039b58f6f786450a858b2c8d2459a3382cc
-- msgbuf not needed.


btcoex.c is about wifi and bluetooth coexistence handling