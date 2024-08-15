FROM phusion/baseimage:focal-1.0.0
LABEL maintainer="BiblePay Developers <dev@biblepay.org>"
LABEL description="Dockerised BiblePayCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/biblepay

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} biblepay && \
    useradd -u ${USER_ID} -g biblepay -s /bin/bash -m -d /home/biblepay biblepay && \
    mkdir /home/biblepay/.biblepaycore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/biblepay

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER biblepay

VOLUME ["/home/biblepay"]

EXPOSE 9998 9999 19998 19999

WORKDIR /home/biblepay
