/*
 * tiBackup - Prio-1 fix regression tests (QtTest).
 *
 * Covers the security/thread-safety fixes:
 *   - ticonf value-semantics + mutex (config UAF fix)
 *   - pbsClient TLS fingerprint helpers (PBS MITM fix)
 *   - passwordhash round-trip / upgrade detection
 *
 * Build with -DBUILD_TESTING=ON. Run under ASan/TSan for the concurrency case:
 *   cmake -B build -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS="-fsanitize=address" && ctest
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QCoreApplication>

#include <atomic>
#include <thread>
#include <vector>

#include "ticonf.h"
#include "pbsclient.h"
#include "tibackupscheduler.h"
#include "obj/pbserver.h"
#include "obj/tibackupjob.h"
#include "webserver/auth/passwordhash.h"

// ---------------------------------------------------------------------------
// Config store: value semantics + concurrency (the use-after-free fix).
// ---------------------------------------------------------------------------
class TiConfTest : public QObject
{
    Q_OBJECT
private slots:
    void valueSemantics()
    {
        PBServer s;
        s.name = "unit-test-pbs";
        s.host = "pbs.example.invalid";
        s.port = 8007;
        s.username = "tester@pbs";
        s.fingerprint = "AA:BB:CC";
        const QString uuid = s.uuid;   // ctor generated one

        tiConfPBServers::instance()->saveItem(s);

        std::optional<PBServer> got = tiConfPBServers::instance()->getItemByUuid(uuid);
        QVERIFY(got.has_value());
        QCOMPARE(got->name, QStringLiteral("unit-test-pbs"));
        QCOMPARE(got->host, QStringLiteral("pbs.example.invalid"));
        QCOMPARE(got->fingerprint, QStringLiteral("AA:BB:CC"));

        // The returned value is a copy: a concurrent re-read must not affect it.
        (void)tiConfPBServers::instance()->getItems();   // triggers readItems() (clears+refills)
        QCOMPARE(got->name, QStringLiteral("unit-test-pbs"));   // copy still intact

        QVERIFY(!tiConfPBServers::instance()->getItemByUuid("does-not-exist").has_value());

        tiConfPBServers::instance()->removeItemByUuid(uuid);
        QVERIFY(!tiConfPBServers::instance()->getItemByUuid(uuid).has_value());
    }

    // tiBackupJob save -> load roundtrip through the value-semantics config store.
    // Guards the QList<tiBackupJob> serialization in ticonf.cpp (incl. the
    // flattened ssh_targets and pbs_backup_ids arrays) that the UAF fix rewrote.
    void jobRoundTrip()
    {
        tiConfBackupJobs jobs;

        tiBackupJob j;
        j.name = "rt-job";
        j.device = "/dev/sdz";
        j.partition_uuid = "uuid-rt-1234";
        j.backupdirs.insert("/src/a", "%MNTBACKUPDIR%/a");
        j.delete_add_file_on_dest = false;
        j.start_backup_on_hotplug = true;
        j.save_log = true;
        j.compare_via_checksum = true;
        j.notify = false;
        j.intervalType = tiBackupJobInterval::DAILY;
        j.intervalTime = "03:30";
        j.intervalDay = 0;
        j.pbs = true;
        j.pbs_server_uuid = "pbs-uuid";
        j.pbs_server_storage = "backup-ds";
        j.pbs_backup_ids = QList<QString>{ "ct/100", "vm/101" };
        j.pbs_dest_folder = "%MNTBACKUPDIR%/pbs";
        j.ssh = true;
        tiBackupJobSSHTarget t;
        t.server_uuid = "ssh-uuid";
        t.backupdirs.insert("/remote/etc", "%MNTBACKUPDIR%/etc");
        j.ssh_targets.append(t);

        jobs.saveBackupJob(j);

        std::optional<tiBackupJob> got = jobs.getJobByName("rt-job");
        QVERIFY(got.has_value());
        QCOMPARE(got->device, QStringLiteral("/dev/sdz"));
        QCOMPARE(got->partition_uuid, QStringLiteral("uuid-rt-1234"));
        QVERIFY(got->start_backup_on_hotplug);
        QVERIFY(got->save_log);
        QVERIFY(got->compare_via_checksum);
        QCOMPARE(got->backupdirs.value("/src/a"), QStringLiteral("%MNTBACKUPDIR%/a"));
        QCOMPARE(got->intervalType, tiBackupJobInterval::DAILY);
        QCOMPARE(got->intervalTime, QStringLiteral("03:30"));
        QVERIFY(got->pbs);
        QCOMPARE(got->pbs_server_storage, QStringLiteral("backup-ds"));
        QCOMPARE(got->pbs_backup_ids.size(), 2);
        QVERIFY(got->pbs_backup_ids.contains("ct/100"));
        QVERIFY(got->pbs_backup_ids.contains("vm/101"));
        QVERIFY(got->ssh);
        QCOMPARE(got->ssh_targets.size(), 1);
        QCOMPARE(got->ssh_targets.at(0).server_uuid, QStringLiteral("ssh-uuid"));
        QCOMPARE(got->ssh_targets.at(0).backupdirs.value("/remote/etc"),
                 QStringLiteral("%MNTBACKUPDIR%/etc"));

        // delete rounds out the on-disk lifecycle.
        QVERIFY(jobs.removeJobByName("rt-job"));
        QVERIFY(!jobs.getJobByName("rt-job").has_value());
    }

    // A held copy stays valid while another thread churns the singleton's list.
    // Under ASan this proves the fix; before it, the held raw pointer dangled.
    void concurrentReadWrite()
    {
        std::vector<QString> uuids;
        for(int i = 0; i < 8; ++i)
        {
            PBServer s;
            s.name = QString("srv-%1").arg(i);
            s.host = QString("h%1.invalid").arg(i);
            uuids.push_back(s.uuid);
            tiConfPBServers::instance()->saveItem(s);
        }

        std::atomic<bool> stop{false};
        std::atomic<int> mismatches{0};

        std::thread reader([&]() {
            while(!stop.load())
            {
                for(const QString &u : uuids)
                {
                    std::optional<PBServer> v = tiConfPBServers::instance()->getItemByUuid(u);
                    if(v && !v->host.endsWith(".invalid"))
                        mismatches.fetch_add(1);   // torn/dangling read
                }
            }
        });
        std::thread writer([&]() {
            for(int n = 0; n < 200; ++n)
                (void)tiConfPBServers::instance()->getItems();   // repeated readItems()
        });

        writer.join();
        stop.store(true);
        reader.join();

        for(const QString &u : uuids)
            tiConfPBServers::instance()->removeItemByUuid(u);

        QCOMPARE(mismatches.load(), 0);
    }
};

// ---------------------------------------------------------------------------
// PBS TLS fingerprint helpers (pinning fix). Pure, no network.
// ---------------------------------------------------------------------------
class FingerprintTest : public QObject
{
    Q_OBJECT
private slots:
    void normalize()
    {
        QCOMPARE(pbsClient::normalizeFingerprint("ab:CD:ef"), QStringLiteral("ABCDEF"));
        QCOMPARE(pbsClient::normalizeFingerprint("  aa bb  "), QStringLiteral("AABB"));
        QCOMPARE(pbsClient::normalizeFingerprint(QString()), QString());
    }

    void matches()
    {
        QVERIFY(pbsClient::fingerprintMatches("ab:cd:ef", "AB:CD:EF"));
        QVERIFY(pbsClient::fingerprintMatches("ABCDEF", "ab:cd:ef"));
        QVERIFY(!pbsClient::fingerprintMatches("ab:cd:ef", "ab:cd:e0"));
        QVERIFY(!pbsClient::fingerprintMatches("", "ab:cd"));          // empty never matches
        QVERIFY(!pbsClient::fingerprintMatches("ab:cd", ""));
        QVERIFY(!pbsClient::fingerprintMatches("abcd", "abcdef"));     // length mismatch
    }
};

// ---------------------------------------------------------------------------
// Password hashing (PBKDF2 + legacy detection).
// ---------------------------------------------------------------------------
class PasswordHashTest : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip()
    {
        const QByteArray salt = passwordhash::generateSalt();
        QVERIFY(!salt.isEmpty());
        const QString h = passwordhash::hash("correct horse battery", salt);
        QVERIFY(h.startsWith("pbkdf2_sha256$"));
        QVERIFY(passwordhash::verify("correct horse battery", salt, h));
        QVERIFY(!passwordhash::verify("wrong password", salt, h));
    }

    void upgradeDetection()
    {
        const QByteArray salt = passwordhash::generateSalt();
        const QString h = passwordhash::hash("pw", salt);
        QVERIFY(!passwordhash::needsUpgrade(h));               // already PBKDF2
        QVERIFY(passwordhash::needsUpgrade("deadbeefcafe"));   // bare legacy hex -> upgrade
    }
};

// ---------------------------------------------------------------------------
// PBS backup-id validation contract (Prio-2 Fix B). Mirrors validPbsBackupId()
// in webserver/apirouter.cpp, which is a file-local helper (not linkable here).
// The live endpoint is exercised end-to-end in test/e2e-prio2.py; this locks the
// accept/reject contract as a fast, root-free tripwire against regex mistakes.
// ---------------------------------------------------------------------------
class PbsBackupIdTest : public QObject
{
    Q_OBJECT
private:
    static bool valid(const QString &s)
    {
        static const QRegularExpression re(QStringLiteral("^(vm|ct|host)/[A-Za-z0-9._-]+$"));
        return re.match(s).hasMatch();
    }
private slots:
    void accepts()
    {
        QVERIFY(valid("vm/101"));
        QVERIFY(valid("ct/100"));
        QVERIFY(valid("host/srv1"));
        QVERIFY(valid("vm/1.2_3-4"));
    }
    void rejects()
    {
        QVERIFY(!valid(""));            // empty
        QVERIFY(!valid("vm"));          // no slash -> would crash the split() indexer
        QVERIFY(!valid("vm/"));         // empty id
        QVERIFY(!valid("/101"));        // empty type
        QVERIFY(!valid("kvm/1"));       // unknown type
        QVERIFY(!valid("VM/1"));        // wrong case
        QVERIFY(!valid("host/a/b"));    // extra slash (id may not contain '/')
        QVERIFY(!valid("vm/1;rm -rf")); // shell metacharacters / spaces
    }
};

// ---------------------------------------------------------------------------
// Scheduler decision (Prio-3 Fix A). Pure, wall-clock-free: strict-slot firing
// with a persisted last-run, so no double-fire and no resurrecting missed slots.
// ---------------------------------------------------------------------------
class SchedulerTest : public QObject
{
    Q_OBJECT
private:
    static tiBackupJob job(tiBackupJobInterval type, const QString &time, int day = 0)
    {
        tiBackupJob j;
        j.name = "sched";
        j.intervalType = type;
        j.intervalTime = time;
        j.intervalDay = day;
        return j;
    }
private slots:
    void daily()
    {
        const tiBackupJob j = job(tiBackupJobInterval::DAILY, "03:00");
        const QDateTime slot(QDate(2026, 7, 17), QTime(3, 0));

        QVERIFY(tiBackupScheduler::shouldRun(j, slot, 0));                    // exactly at slot
        QVERIFY(tiBackupScheduler::shouldRun(j, slot.addSecs(30), 0));        // within grace
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot.addSecs(-60), 0));      // before slot
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot.addSecs(300), 0));      // past grace -> missed, skip

        // Already fired this slot (last-run at/after the slot) -> no re-fire.
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot.addSecs(30), slot.toSecsSinceEpoch()));
        // Last run was for the PREVIOUS occurrence -> fire.
        QVERIFY(tiBackupScheduler::shouldRun(j, slot, slot.toSecsSinceEpoch() - 1));
    }

    void graceBoundary()
    {
        const tiBackupJob j = job(tiBackupJobInterval::DAILY, "03:00");
        const QDateTime slot(QDate(2026, 7, 17), QTime(3, 0));
        QVERIFY(tiBackupScheduler::shouldRun(j, slot.addSecs(tiBackupScheduler::defaultGraceSecs), 0));       // inclusive
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot.addSecs(tiBackupScheduler::defaultGraceSecs + 1), 0));  // one past
    }

    void weekly()
    {
        const QDate d(2026, 7, 17);
        const QDateTime slot(d, QTime(3, 0));
        const int today = d.dayOfWeek() - 1;                 // 0=Mon..6=Sun (as stored)

        tiBackupJob j = job(tiBackupJobInterval::WEEKLY, "03:00", today);
        QVERIFY(tiBackupScheduler::shouldRun(j, slot, 0));   // matching weekday

        j.intervalDay = (today + 1) % 7;                     // a different weekday -> no slot today
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot, 0));
    }

    void monthly()
    {
        const QDate d(2026, 7, 17);
        const QDateTime slot(d, QTime(3, 0));

        tiBackupJob j = job(tiBackupJobInterval::MONTHLY, "03:00", d.day());
        QVERIFY(tiBackupScheduler::shouldRun(j, slot, 0));   // matching day-of-month

        j.intervalDay = d.day() == 28 ? 27 : 28;             // a different day -> no slot today
        QVERIFY(!tiBackupScheduler::shouldRun(j, slot, 0));
    }

    void neverRuns()
    {
        const QDateTime slot(QDate(2026, 7, 17), QTime(3, 0));
        QVERIFY(!tiBackupScheduler::shouldRun(job(tiBackupJobInterval::NONE, "03:00"), slot, 0));   // NONE
        QVERIFY(!tiBackupScheduler::shouldRun(job(tiBackupJobInterval::DAILY, "nonsense"), slot, 0)); // bad time
        QVERIFY(!tiBackupScheduler::shouldRun(job(tiBackupJobInterval::DAILY, ""), slot, 0));         // empty time
    }
};

int main(int argc, char *argv[])
{
    // Redirect the whole config tree into a throwaway dir BEFORE any tiConfMain
    // is constructed (the config singletons cache it on first use).
    static QTemporaryDir confDir;
    qputenv("TIBACKUP_CONF", (confDir.path() + "/main.conf").toLocal8Bit());

    QCoreApplication app(argc, argv);

    int status = 0;
    { TiConfTest t;        status |= QTest::qExec(&t, argc, argv); }
    { FingerprintTest t;   status |= QTest::qExec(&t, argc, argv); }
    { PasswordHashTest t;  status |= QTest::qExec(&t, argc, argv); }
    { PbsBackupIdTest t;   status |= QTest::qExec(&t, argc, argv); }
    { SchedulerTest t;     status |= QTest::qExec(&t, argc, argv); }
    return status;
}

#include "tst_prio1.moc"
