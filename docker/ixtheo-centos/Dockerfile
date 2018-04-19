FROM centos:latest
MAINTAINER Mario Trojan <mario.trojan@uni-tuebingen.de>

# add needed configuration files
# (+simulate mounted /mnt/ZE020150)
ADD mnt/ZE020150 /mnt/ZE020150

# set java encoding to avoid problems when compiling solr plugins
ENV JAVA_TOOL_OPTIONS="-Dfile.encoding=UTF8"

# download & run ubtools installation script
# (use curl because it's part of standard centos installation)
RUN curl https://raw.githubusercontent.com/ubtue/ub_tools/master/cpp/data/installer/scripts/install_ubtools.sh -o /tmp/install_ubtools.sh \
    && chmod 700 /tmp/install_ubtools.sh
RUN /tmp/install_ubtools.sh ixtheo --omit-cronjobs --omit-systemctl

# add local config
ADD local_overrides/* /usr/local/vufind/local/tuefind/local_overrides/

# build css
RUN php /usr/local/vufind/util/cssBuilder.php

# expose solr & import example marc data
EXPOSE 8080
ADD data.mrc /usr/local/ub_tools/bsz_daten/data.mrc
RUN source /etc/profile.d/vufind.sh \
    && sudo -u solr /usr/local/vufind/solr.sh start \
    && /usr/local/vufind/import-marc.sh /usr/local/ub_tools/bsz_daten/data.mrc

# configure mariadb (needs to be done via temp file)
# (see https://stackoverflow.com/questions/26982905/mysql-setup-using-docker)
EXPOSE 3306
RUN mysql_install_db --user=mysql --ldata=/var/lib/mysql/ --basedir=/usr
RUN echo "mysqld_safe &" > /tmp/config \
    && echo "mysqladmin --silent --wait=30 ping || exit 1" >> /tmp/config \
    && echo "mysql -u root -e \"CREATE DATABASE vufind;\"" >> /tmp/config \
    && echo "mysql -u root vufind < /usr/local/vufind/module/VuFind/sql/mysql.sql" >> /tmp/config \
    && echo "mysql -u root vufind < /usr/local/vufind/module/IxTheo/sql/mysql.sql" >> /tmp/config \
    && echo "mysql -u root -e \"CREATE USER vufind@localhost IDENTIFIED BY 'vufind';\"" >> /tmp/config \
    && echo "mysql -u root -e \"GRANT ALL ON *.* TO vufind@localhost;\"" >> /tmp/config \
    && echo "mysql -u root -e \"FLUSH PRIVILEGES;\"" >> /tmp/config \
    && bash /tmp/config \
    && rm -f /tmp/config

# configure apache
EXPOSE 80
EXPOSE 443
ADD httpd/*.conf /etc/httpd/conf.d/
ADD httpd/*.pem /etc/ssl/certs/

# start services when container is run
ADD startup.sh /startup.sh
CMD [ "/startup.sh" ]