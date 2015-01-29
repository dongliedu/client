/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QStringList>
#include <csync.h>
#include <QMap>
#include "networkjobs.h"
#include <QMutex>
#include <QWaitCondition>
#include <QLinkedList>

namespace OCC {

class Account;

/**
 * The Discovery Phase was once called "update" phase in csync therms.
 * Its goal is to look at the files in one of the remote and check comared to the db
 * if the files are new, or changed.
 */

struct DiscoveryDirectoryResult {
    QString path;
    QString msg;
    int code;
    QLinkedList<csync_vio_file_stat_t*>::iterator iterator;
    QLinkedList<csync_vio_file_stat_t *> list;
};

// Run in the main thread, reporting to the DiscoveryJobMainThread object
class DiscoverySingleDirectoryJob : public QObject {
    Q_OBJECT
public:
    explicit DiscoverySingleDirectoryJob(AccountPtr account, const QString &path, QObject *parent = 0);
    void start();
    void abort();
    // This is not actually a network job, it is just a job
signals:
    void firstDirectoryPermissions(const QString &);
    void firstDirectoryEtag(const QString &);
    void finishedWithResult(QLinkedList<csync_vio_file_stat_t*>);
    void finishedWithError(int csyncErrnoCode, QString msg);
private slots:
    void directoryListingIteratedSlot(QString,QMap<QString,QString>);
    void lsJobFinishedWithoutErrorSlot();
    void lsJobFinishedWithErrorSlot(QNetworkReply*);
private:
    QLinkedList<csync_vio_file_stat_t*> _results;
    QString _subPath;
    AccountPtr _account;
    bool _ignoredFirst;
    QPointer<LsColJob> _lsColJob;
};

// Lives in main thread. Deleted by the SyncEngine
class DiscoveryJob;
class DiscoveryMainThread : public QObject {
    Q_OBJECT

    // For non-recursive and recursive
    // If it is not in this map it needs to be requested
    QMap<QString, QLinkedList<csync_vio_file_stat_t*> > _directoryContents;


    QPointer<DiscoveryJob> _discoveryJob;
    QPointer<DiscoverySingleDirectoryJob> _singleDirJob;
    QString _pathPrefix;
    AccountPtr _account;
    DiscoveryDirectoryResult *_currentDiscoveryDirectoryResult;

public:
    DiscoveryMainThread(AccountPtr account) : QObject(), _account(account), _currentDiscoveryDirectoryResult(0) {

    }
    ~DiscoveryMainThread() {
        QMutableMapIterator<QString, QLinkedList<csync_vio_file_stat_t*> > im(_directoryContents);
         while (im.hasNext()) {
             im.next();
             QMutableLinkedListIterator<csync_vio_file_stat_t*> il(im.value());
             while (il.hasNext()){
                 il.next();
                 csync_vio_file_stat_destroy(il.value());
                 il.remove();
             }
             im.remove();
         }
    }
    void abort();


public slots:
    // From DiscoveryJob:
    void doOpendirSlot(QString url, DiscoveryDirectoryResult* );

    // From Job:
    void singleDirectoryJobResultSlot(QLinkedList<csync_vio_file_stat_t*>);
    void singleDirectoryJobFinishedWithErrorSlot(int csyncErrnoCode, QString msg);
    void singleDirectoryJobFirstDirectoryPermissionsSlot(QString);
signals:
    void rootEtag(QString);
public:
    void setupHooks(DiscoveryJob* discoveryJob, const QString &pathPrefix);
};


// Lives in the other thread
// Deletes itself in start()
class DiscoveryJob : public QObject {
    Q_OBJECT
    friend class DiscoveryMainThread;
    CSYNC *_csync_ctx;
    csync_log_callback _log_callback;
    int _log_level;
    void* _log_userdata;
    QElapsedTimer lastUpdateProgressCallbackCall;

    /**
     * return true if the given path should be synced,
     * false if the path should be ignored
     */
    bool isInSelectiveSyncBlackList(const QString &path) const;
    static int isInSelectiveSyncBlackListCallBack(void *, const char *);

    // Just for progress
    static void update_job_update_callback (bool local,
                                            const char *dirname,
                                            void *userdata);

    // For using QNAM to get the directory listings
    static csync_vio_handle_t* remote_vio_opendir_hook (const char *url,
                                        void *userdata);
    static csync_vio_file_stat_t* remote_vio_readdir_hook (csync_vio_handle_t *dhandle,
                                                                  void *userdata);
    static void remote_vio_closedir_hook (csync_vio_handle_t *dhandle,
                                                                  void *userdata);
    QMutex _vioMutex;
    QWaitCondition _vioWaitCondition;


public:
    explicit DiscoveryJob(CSYNC *ctx, QObject* parent = 0)
            : QObject(parent), _csync_ctx(ctx) {
        // We need to forward the log property as csync uses thread local
        // and updates run in another thread
        _log_callback = csync_get_log_callback();
        _log_level = csync_get_log_level();
        _log_userdata = csync_get_log_userdata();
    }

    QStringList _selectiveSyncBlackList;
    Q_INVOKABLE void start();
signals:
    void finished(int result);
    void folderDiscovered(bool local, QString folderUrl);

    // After the discovery job has been woken up again (_vioWaitCondition)
    void doOpendirSignal(QString url, DiscoveryDirectoryResult*);
};

}