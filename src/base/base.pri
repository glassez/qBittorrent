HEADERS += \
    $$PWD/types.h \
    $$PWD/tristatebool.h \
    $$PWD/filesystemwatcher.h \
    $$PWD/qinisettings.h \
    $$PWD/logger.h \
    $$PWD/settingsstorage.h \
    $$PWD/preferences.h \
    $$PWD/iconprovider.h \
    $$PWD/htmlparser.h \
    $$PWD/http/irequesthandler.h \
    $$PWD/http/connection.h \
    $$PWD/http/requestparser.h \
    $$PWD/http/responsegenerator.h \
    $$PWD/http/server.h \
    $$PWD/http/types.h \
    $$PWD/http/responsebuilder.h \
    $$PWD/net/dnsupdater.h \
    $$PWD/net/downloadmanager.h \
    $$PWD/net/downloadhandler.h \
    $$PWD/net/geoipmanager.h \
    $$PWD/net/portforwarder.h \
    $$PWD/net/reverseresolution.h \
    $$PWD/net/smtp.h \
    $$PWD/net/private/geoipdatabase.h \
    $$PWD/bittorrent/infohash.h \
    $$PWD/bittorrent/session.h \
    $$PWD/bittorrent/sessionstatus.h \
    $$PWD/bittorrent/cachestatus.h \
    $$PWD/bittorrent/magneturi.h \
    $$PWD/bittorrent/torrentinfo.h \
    $$PWD/bittorrent/torrenthandle.h \
    $$PWD/bittorrent/peerinfo.h \
    $$PWD/bittorrent/trackerentry.h \
    $$PWD/bittorrent/tracker.h \
    $$PWD/bittorrent/torrentcreatorthread.h \
    $$PWD/bittorrent/private/speedmonitor.h \
    $$PWD/bittorrent/private/bandwidthscheduler.h \
    $$PWD/bittorrent/private/filterparserthread.h \
    $$PWD/bittorrent/private/statistics.h \
    $$PWD/bittorrent/private/resumedatasavingmanager.h \
    $$PWD/rss/rssmanager.h \
    $$PWD/rss/rssfeed.h \
    $$PWD/rss/rssfolder.h \
    $$PWD/rss/rssfile.h \
    $$PWD/rss/rssarticle.h \
    $$PWD/rss/rssdownloadrule.h \
    $$PWD/rss/rssdownloadrulelist.h \
    $$PWD/rss/private/rssparser.h \
    $$PWD/utils/fs.h \
    $$PWD/utils/gzip.h \
    $$PWD/utils/json.h \
    $$PWD/utils/misc.h \
    $$PWD/utils/string.h \
    $$PWD/unicodestrings.h \
    $$PWD/torrentfilter.h \
    $$PWD/scanfoldersmodel.h \
    $$PWD/search/searchengine.h \
    $$PWD/search/private/searchworker.h \
    $$PWD/search/private/luastate.h \
    $$PWD/search/private/luafunctions.h

SOURCES += \
    $$PWD/tristatebool.cpp \
    $$PWD/filesystemwatcher.cpp \
    $$PWD/logger.cpp \
    $$PWD/settingsstorage.cpp \
    $$PWD/preferences.cpp \
    $$PWD/iconprovider.cpp \
    $$PWD/htmlparser.cpp \
    $$PWD/http/connection.cpp \
    $$PWD/http/requestparser.cpp \
    $$PWD/http/responsegenerator.cpp \
    $$PWD/http/server.cpp \
    $$PWD/http/responsebuilder.cpp \
    $$PWD/net/dnsupdater.cpp \
    $$PWD/net/downloadmanager.cpp \
    $$PWD/net/downloadhandler.cpp \
    $$PWD/net/geoipmanager.cpp \
    $$PWD/net/portforwarder.cpp \
    $$PWD/net/reverseresolution.cpp \
    $$PWD/net/smtp.cpp \
    $$PWD/net/private/geoipdatabase.cpp \
    $$PWD/bittorrent/infohash.cpp \
    $$PWD/bittorrent/session.cpp \
    $$PWD/bittorrent/sessionstatus.cpp \
    $$PWD/bittorrent/cachestatus.cpp \
    $$PWD/bittorrent/magneturi.cpp \
    $$PWD/bittorrent/torrentinfo.cpp \
    $$PWD/bittorrent/torrenthandle.cpp \
    $$PWD/bittorrent/peerinfo.cpp \
    $$PWD/bittorrent/trackerentry.cpp \
    $$PWD/bittorrent/tracker.cpp \
    $$PWD/bittorrent/torrentcreatorthread.cpp \
    $$PWD/bittorrent/private/speedmonitor.cpp \
    $$PWD/bittorrent/private/bandwidthscheduler.cpp \
    $$PWD/bittorrent/private/filterparserthread.cpp \
    $$PWD/bittorrent/private/statistics.cpp \
    $$PWD/bittorrent/private/resumedatasavingmanager.cpp \
    $$PWD/rss/rssmanager.cpp \
    $$PWD/rss/rssfeed.cpp \
    $$PWD/rss/rssfolder.cpp \
    $$PWD/rss/rssarticle.cpp \
    $$PWD/rss/rssdownloadrule.cpp \
    $$PWD/rss/rssdownloadrulelist.cpp \
    $$PWD/rss/rssfile.cpp \
    $$PWD/rss/private/rssparser.cpp \
    $$PWD/utils/fs.cpp \
    $$PWD/utils/gzip.cpp \
    $$PWD/utils/misc.cpp \
    $$PWD/utils/string.cpp \
    $$PWD/torrentfilter.cpp \
    $$PWD/scanfoldersmodel.cpp \
    $$PWD/search/searchengine.cpp \
    $$PWD/search/private/searchworker.cpp \
    $$PWD/search/private/luastate.cpp \
    $$PWD/search/private/luafunctions.cpp

RESOURCES += \
    $$PWD/search/search.qrc

# QJson JSON parser/serializer for using with Qt4
lessThan(QT_MAJOR_VERSION, 5) {
    !usesystemqjson: include(3rdparty/qjson/qjson.pri)
    else: DEFINES += USE_SYSTEM_QJSON
}

include(3rdparty/lua/lua.pri)
