FROM centos:8
MAINTAINER Johannes Riedl <johannes.riedl@uni-tuebingen.de>

# add needed configuration files
# (+simulate mounted /mnt/ZE020150)
ADD mnt/ZE020150 /mnt/ZE020150
ADD Elasticsearch.conf /usr/local/var/lib/tuelib/Elasticsearch.conf
# set java encoding to avoid problems when compiling solr plugins
ENV JAVA_TOOL_OPTIONS="-Dfile.encoding=UTF8"

# download & run ubtools installation script
# (use curl because it's part of standard centos installation)
RUN curl https://raw.githubusercontent.com/ubtue/ub_tools/master/cpp/data/installer/scripts/install_ubtools.sh -o /tmp/install_ubtools.sh \
    && chmod 700 /tmp/install_ubtools.sh
RUN dnf --assumeyes install dnf-plugin-config-manager
RUN dnf config-manager --add-repo https://download.opensuse.org/repositories/home:/Alexander_Pozdnyakov/CentOS_8/
RUN rpm --import https://build.opensuse.org/projects/home:Alexander_Pozdnyakov/public_key
RUN dnf --assumeyes update
RUN dnf --assumeyes install tesseract-devel net-tools vim
RUN /tmp/install_ubtools.sh --production --fulltext-backend

# configure ES
EXPOSE 9200

# start services when container is run
ADD startup.sh /startup.sh
ADD elasticsearch.yml /etc/elasticsearch/elasticsearch.yml
CMD [ "/startup.sh" ]
