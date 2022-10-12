<html>
<header>
    <title>tiBackup Backupstatus</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.2.2/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-Zenh87qX5JnK2Jl0vWa8Ck2rdkQ2Bzep5IDxbcnCeuOxjzrPF/et3URy9Bv1WTRi" crossorigin="anonymous">
    <link rel="stylesheet" type="text/css" href="https://cdn.datatables.net/1.12.1/css/dataTables.bootstrap5.min.css">
    <script type="text/javascript" language="javascript" src="https://code.jquery.com/jquery-3.5.1.js"></script>
    <script type="text/javascript" language="javascript" src="https://cdn.datatables.net/1.12.1/js/jquery.dataTables.min.js"></script>
    <script type="text/javascript" language="javascript" src="https://cdn.datatables.net/1.12.1/js/dataTables.bootstrap5.min.js"></script>
</header>
<body>
<div class="col-12 p-2" style="background-color: #eef3f8;">
    <div class="row">
        <div class="col-2"><a href="/"><img src="/tibackup.png" style="width: 60px"></a></div>
        <div class="col-4"><h1>tiBackup Backupstatus</h1></div>
        <div class="col-4" style="margin-top: auto; margin-bottom: auto;">Information time: <span id="info_time" style="color: darkgreen;">time</span> <a href="javascript: location.reload();">Refresh</a></div>
        <div class="col text-right"><img src="/iteas-logo.png" style="width: 150px"></div>
    </div>
</div>
<div class="row p-2">
    <div class="col-6">
        <div class="row">
            <div class="col"><h3>Backupjobs:</h3></div>
        </div>
        <div class="row p-2">
            <table class="table" id="tbackupjobs">
                <thead>
                    <th scope="col">Name</th>
                    <th scope="col">Device</th>
                    <th scope="col">Partition-UUID</th>
                    <th scope="col">Status</th>
                </thead>
                <tbody>
                {loop jobs}
                    <tr>
                        <td>{jobs.name}</td>
                        <td>{jobs.device}</td>
                        <td>{jobs.partition_uuid}</td>
                        <td>{jobs.status}</td>
                    </tr>
                {end jobs}
                </tbody>
            </table>
        </div>
    </div>
    <div class="col-6">
        <div class="row">
            <div class="col"><h3>Last Backups and Logs:</h3></div>
        </div>
        <div class="row p-2">
            <table class="table" id="tbackuplog">
                <thead>
                <th scope="col">Date</th>
                <th scope="col">Name</th>
                </thead>
                <tbody>
                {loop backuplog}
                <tr>
                    <td><a href="/backuplog?name={backuplog.file}">{backuplog.date}<a/></td>
                    <td>{backuplog.name}</td>
                </tr>
                {end backuplog}
                </tbody>
            </table>
        </div>
    </div>
</div>
<script>
    // self executing function here
    (function() {
        let d = new Date();
        let dstring = `${d.toTimeString()}`.split(" ");
        document.getElementById("info_time").innerText = dstring[0];

        $('#tbackuplog').DataTable({
            order: [[0, 'desc']],
        });
        $('#tbackupjobs').DataTable({
            order: [[0, 'asc']],
        });
    })();
</script>
</body>
</header>
</html>
