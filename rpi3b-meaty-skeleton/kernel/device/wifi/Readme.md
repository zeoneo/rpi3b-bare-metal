BCM43430 uses sdio interface then we need to use bcdc protocol, for pcie we need to use msgbuf protocol.

Reference https://git.congatec.com/android/qmx6_kernel/commit/f1d56039b58f6f786450a858b2c8d2459a3382cc
-- msgbuf not needed.


btcoex.c is about wifi and bluetooth coexistence handling


SD/SDIO card commands description
https://yannik520.github.io/sdio.html

https://www.silabs.com/community/mcu/32-bit/knowledge-base.entry.html/2019/01/18/sdio_initial_driver-JXnL

https://www.cs.utexas.edu/~simon/395t_os/resources/Part_1_Physical_Layer_Simplified_Specification_Ver_3.01_Final_100518.pdf