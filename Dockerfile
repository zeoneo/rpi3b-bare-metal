FROM i386/ubuntu:18.04
RUN apt-get update \
  && apt-get install -y wget git make \
  && rm -rf /var/lib/apt/lists/*

RUN wget "https://armkeil.blob.core.windows.net/developer//sitecore/shell/-/media/Files/downloads/gnu-rm/5_4-2016q3/gcc-arm-none-eabi-5_4-2016q3-20160926-linux,-d-,tar.bz2"

RUN tar xvf gcc-arm-none-eabi-5_4-2016q3-20160926-linux,-d-,tar.bz2
RUN rm gcc-arm-none-eabi-5_4-2016q3-20160926-linux,-d-,tar.bz2
RUN git clone https://github.com/zeoneo/rpi3b-bare-metal.git
ENV PATH $PATH:/gcc-arm-none-eabi-5_4-2016q3/bin
WORKDIR /rpi3b-bare-metal/rpi3b-meaty-skeleton
