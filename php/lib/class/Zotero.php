<?php


namespace Zotero;

/**
 * Class for the Zotero Translation Server (using c++ zts_client and zotero_crawler)
 */
class MetadataHarvester {

    /**
     * Main directory for zts client maps
     */
    const DIR_ZTS_CLIENT_MAPS = '/usr/local/ub_tools/cpp/data/zts_client_maps/';

    /**
     * Example config file for crawler
     */
    const FILE_CRAWLER_EXAMPLE = '/usr/local/ub_tools/cpp/data/zotero_crawler.conf';

    /**
     * URL of Server
     * @var string
     */
    protected $Url;

    /**
     * Initialize new Server
     * @param string $Url
     */
    public function __construct($Url) {
        $this->Url = $Url;
    }

    /**
     * Start crawling, using zts_client
     *
     * @param string $UrlBase
     * @param string $UrlRegex
     * @param int $Depth
     * @param bool $IgnoreRobots
     * @param string $FileExtension     supported extension, e.g. "xml" for MARCXML or "mrc" for MARC21
     * @return \Zotero\BackgroundTask
     */
    public function start($UrlBase, $UrlRegex, $Depth, $IgnoreRobots, $FileExtension) {
        $uniqid = uniqid('Zts_' . date('Y-m-d_H-i-s_'));
        $CfgPath = DIR_TMP . $uniqid . '.conf';

        // generate local copy of zts_client_maps
        $DirMap = self::DIR_ZTS_CLIENT_MAPS;
        $DirMapLocal = DIR_TMP . 'ZtsMap/';
        if (!is_dir($DirMapLocal)) {
            copy_recursive($DirMap, $DirMapLocal);

            // reset previously_downloaded.hashes
            $FilePrevDownloaded = $DirMapLocal . 'previously_downloaded.hashes';
            unlink($FilePrevDownloaded);
            symlink('/dev/null', $FilePrevDownloaded);
        }

        // only .mrc or .xml (type will be auto detected)
        $OutPath = DIR_TMP . $uniqid . '.' . $FileExtension;
        $ProgressPath = DIR_TMP . $uniqid . '.progress';

        self::_writeConfigFile($CfgPath, $UrlBase, $UrlRegex, $Depth);
        return $this->_executeCommand($uniqid, $CfgPath, $DirMapLocal, $OutPath, $IgnoreRobots);
    }

    /**
     * Call zts_client (start background task, dont wait for result)
     *
     * @param string $Id
     * @param string $CfgPath
     * @param string $DirMap
     * @param string $OutPath
     * @param bool $IgnoreRobots
     * @return \Zotero\BackgroundTask
     */
    protected function _executeCommand($Id, $CfgPath, $DirMap, $OutPath, $IgnoreRobots=false) {
        $ProgressPath = BackgroundTask::getProgressPath($Id);

        $cmd = 'zts_client';
        if ($IgnoreRobots) {
            $cmd .= ' --ignore-robots-dot-txt';
        }
        $cmd .= ' --zotero-crawler-config-file="' . $CfgPath . '"';
        if ($ProgressPath != null) {
            $cmd .= ' --progress-file="' . $ProgressPath . '"';
        }
        $cmd .= ' ' . $this->Url . ' ' . $DirMap . ' "' . $OutPath . '"';
        $cmd .= ' 2>&1';

        $task = new BackgroundTask();
        $task->Cmd = $cmd;
        $task->MarcPath = $OutPath;
        $task->TaskId = $Id;
        $descriptorspec = array(0 => array("pipe", "r"),  // stdin is a pipe that the child will read from
                                1 => array("pipe", "w"),  // stdout is a pipe that the child will write to
                                2 => array("pipe", "w"),  // stderr is a pipe that the child will write to);
                                );
        $task->Resource = proc_open($cmd, $descriptorspec, $task->Pipes);

        return $task;
    }

    /**
     * Prepare config file for zts_client
     *
     * @param string $CfgPath
     * @param string $UrlBase
     * @param string $UrlRegex
     * @param int $Depth
     */
    static protected function _writeConfigFile($CfgPath, $UrlBase, $UrlRegex, $Depth) {
        $CfgContents = '# start_URL max_crawl_depth URL_regex' . PHP_EOL;
        $CfgContents .= implode(' ', array($UrlBase, $Depth, $UrlRegex));
        file_put_contents($CfgPath, $CfgContents);
    }
}

/**
 * Class for background tasks, generated as soon as the shell command is executed.
 * Can be used to monitor the status of the running cli subprocess.
 */
class BackgroundTask {
    /**
     * contains the full CLI call (for debug output)
     * @var string
     */
    public $Cmd;

    /**
     * Path to output file
     * @var string
     */
    public $MarcPath;

    /**
     * Task Id. Unique string, can be used for status requests
     * @var string
     */
    public $TaskId;

    /**
     * array of pipes opened to the process. see www.php.net/proc_open
     * @var array
     */
    public $Pipes;

    /**
     * Resource (e.g. for proc_get_status)
     * @var type
     */
    public $Resource;

    /**
     * destructor: close pipes if still open
     */
    function __destruct() {
        @fclose($this->Pipes[0]);
        @fclose($this->Pipes[1]);
        @fclose($this->Pipes[2]);
    }

    /**
     * Get CLI output
     * we only read stdout, because stderr was redirected to stdout in _executeCommand
     *
     * @return string
     */
    public function getOutput() {
        return stream_get_contents($this->Pipes[1]);
    }

    /**
     * Get progress (in percent)
     * @return int
     */
    public function getProgress() {
        $path = self::getProgressPath($this->TaskId);
        if (is_file($path)) {
            $progress_raw = file_get_contents($path);
            if ($progress_raw !== false && $progress_raw !== '') {
                $progress_percent = intval($progress_raw * 100);
                return $progress_percent;
            } else {
                return false;
            }
        }
    }

    /**
     * Get path to progress file (tmp) for a background task
     *
     * @param string $TaskId
     * @return string
     */
    static public function getProgressPath($TaskId) {
        return DIR_TMP . $TaskId . '.progress';
    }

    /**
     * Get status (see www.php.net/proc_get_status)
     */
    public function getStatus() {
        return proc_get_status($this->Resource);
    }
}