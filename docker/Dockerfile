FROM debian:stretch
LABEL maintainer="BiblePay Developers <dev@biblepay.org>"
LABEL description="Dockerised BiblePayCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
