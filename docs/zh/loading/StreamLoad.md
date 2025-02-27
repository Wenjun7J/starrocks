---
displayed_sidebar: docs
keywords: ['Stream Load']
---

# 从本地文件系统导入

import InsertPrivNote from '../_assets/commonMarkdown/insertPrivNote.md'

StarRocks 提供两种导入方式帮助您从本地文件系统导入数据：

- 使用 [Stream Load](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md) 进行同步导入。
- 使用 [Broker Load](../sql-reference/sql-statements/loading_unloading/BROKER_LOAD.md) 进行异步导入。

两种导入方式各有优势：

- Stream Load 支持 CSV 和 JSON 两种数据文件格式，适用于数据文件数量较少且单个文件的大小不超过 10 GB 的场景。
- Broker Load 支持 Parquet、ORC、CSV、及 JSON 四种文件格式（JSON 文件格式自 3.2.3 版本起支持），适用于数据文件数量较多且单个文件的大小超过 10 GB 的场景、以及文件存储在 NAS 的场景。

对于 CSV 格式的数据，需要注意以下两点：

- StarRocks 支持设置长度最大不超过 50 个字节的 UTF-8 编码字符串作为列分隔符，包括常见的逗号 (,)、Tab 和 Pipe (|)。
- 空值 (null) 用 `\N` 表示。比如，数据文件一共有三列，其中某行数据的第一列、第三列数据分别为 `a` 和 `b`，第二列没有数据，则第二列需要用 `\N` 来表示空值，写作 `a,\N,b`，而不是 `a,,b`。`a,,b` 表示第二列是一个空字符串。

Stream Load 和 Broker Load 均支持在导入过程中做数据转换、以及通过 UPSERT 和 DELETE 操作实现数据变更。请参见[导入过程中实现数据转换](../loading/Etl_in_loading.md)和[通过导入实现数据变更](../loading/Load_to_Primary_Key_tables.md)。

## 准备工作

### 查看权限

<InsertPrivNote />

### 查看网络配置

确保待导入数据所在的机器能够访问 StarRocks 集群中 FE 节点的 [`http_port`](../administration/management/FE_configuration.md#http_port) 端口（默认 `8030`）、以及 BE 节点的 [`be_http_port`](../administration/management/BE_configuration.md#be_http_port) 端口（默认 `8040`）。

## 使用 Stream Load 从本地导入

Stream Load 是一种基于 HTTP PUT 的同步导入方式。提交导入作业以后，StarRocks 会同步地执行导入作业，并返回导入作业的结果信息。您可以通过返回的结果信息来判断导入作业是否成功。

> **NOTICE**
>
> Stream Load 操作会同时更新和 StarRocks 原始表相关的物化视图的数据。

### 基本原理

您需要在客户端上通过 HTTP 发送导入作业请求给 FE，FE 会通过 HTTP 重定向 (Redirect) 指令将请求转发给某一个 BE（或 CN）。或者，您也可以直接发送导入作业请求给某一个 BE（或 CN）。

:::note

如果把导入作业请求发送给 FE，FE 会通过轮询机制选定由哪一个 BE（或 CN）来接收请求，从而实现 StarRocks 集群内的负载均衡。因此，推荐您把导入作业请求发送给 FE。

:::

接收导入作业请求的 BE（或 CN）作为 Coordinator BE（或 CN），将数据按表结构划分、并分发数据到其他各相关的 BE（或 CN）。导入作业的结果信息由 Coordinator BE（或 CN）返回给客户端。需要注意的是，如果您在导入过程中停止 Coordinator BE（或 CN），会导致导入作业失败。

下图展示了 Stream Load 的主要流程：

![Stream Load 原理图](../_assets/4.2-1-zh.png)

### 使用限制

Stream Load 当前不支持导入某一列为 JSON 的 CSV 文件的数据。

### 操作示例

本文以 curl 工具为例，介绍如何使用 Stream Load 从本地文件系统导入 CSV 或 JSON 格式的数据。有关创建导入作业的详细语法和参数说明，请参见 [STREAM LOAD](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md)。

注意在 StarRocks 中，部分文字是 SQL 语言的保留关键字，不能直接用于 SQL 语句。如果想在 SQL 语句中使用这些保留关键字，必须用反引号 (`) 包裹起来。参见[关键字](../sql-reference/sql-statements/keywords.md)。

#### 导入 CSV 格式的数据

##### 数据样例

在本地文件系统中创建一个 CSV 格式的数据文件 `example1.csv`。文件一共包含三列，分别代表用户 ID、用户姓名和用户得分，如下所示：

```Plain_Text
1,Lily,23
2,Rose,23
3,Alice,24
4,Julia,25
```

##### 建库建表

通过如下语句创建数据库、并切换至该数据库：

```SQL
CREATE DATABASE IF NOT EXISTS mydatabase;
USE mydatabase;
```

通过如下语句手动创建主键表 `table1`，包含 `id`、`name` 和 `score` 三列，分别代表用户 ID、用户姓名和用户得分，主键为 `id` 列，如下所示：

```SQL
CREATE TABLE `table1`
(
    `id` int(11) NOT NULL COMMENT "用户 ID",
    `name` varchar(65533) NULL COMMENT "用户姓名",
    `score` int(11) NOT NULL COMMENT "用户得分"
)
ENGINE=OLAP
PRIMARY KEY(`id`)
DISTRIBUTED BY HASH(`id`);
```

:::note

自 2.5.7 版本起，StarRocks 支持在建表和新增分区时自动设置分桶数量 (BUCKETS)，您无需手动设置分桶数量。更多信息，请参见 [设置分桶数量](../table_design/data_distribution/Data_distribution.md#设置分桶数量)。

:::

##### 提交导入作业

通过如下命令，把 `example1.csv` 文件中的数据导入到 `table1` 表中：

```Bash
curl --location-trusted -u <username>:<password> -H "label:123" \
    -H "Expect:100-continue" \
    -H "column_separator:," \
    -H "columns: id, name, score" \
    -T example1.csv -XPUT \
    http://<fe_host>:<fe_http_port>/api/mydatabase/table1/_stream_load
```

:::note

- 如果账号没有设置密码，这里只需要传入 `<username>:`。
- 您可以通过 [SHOW FRONTENDS](../sql-reference/sql-statements/cluster-management/nodes_processes/SHOW_FRONTENDS.md) 命令查看 FE 节点的 IP 地址和 HTTP 端口号。

:::

`example1.csv` 文件中包含三列，跟 `table1` 表的 `id`、`name`、`score` 三列一一对应，并用逗号 (,) 作为列分隔符。因此，需要通过 `column_separator` 参数指定列分隔符为逗号 (,)，并且在 `columns` 参数中按顺序把 `example1.csv` 文件中的三列临时命名为 `id`、`name`、`score`。`columns` 参数中声明的三列，按名称对应 `table1` 表中的三列。

导入完成后，您可以查询 `table1` 表，验证数据导入是否成功，如下所示：

```SQL
SELECT * FROM table1;

+------+-------+-------+
| id   | name  | score |
+------+-------+-------+
|    1 | Lily  |    23 |
|    2 | Rose  |    23 |
|    3 | Alice |    24 |
|    4 | Julia |    25 |
+------+-------+-------+

4 rows in set (0.00 sec)
```

#### 导入 JSON 格式的数据

从 3.2.7 版本起，STREAM LOAD 支持在传输过程中对 JSON 数据进行压缩，减少网络带宽开销。用户可以通过 `compression` 或 `Content-Encoding` 参数指定不同的压缩方式，支持 GZIP、BZIP2、LZ4_FRAME、ZSTD 压缩算法。语法参见[STREAM LOAD](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md)。

##### 数据样例

在本地文件系统中创建一个 JSON 格式的数据文件 `example2.json`。文件一共包含两个字段，分别代表城市名称和城市 ID，如下所示：

```JSON
{"name": "北京", "code": 2}
```

##### 建库建表

通过如下语句创建数据库、并切换至该数据库：

```SQL
CREATE DATABASE IF NOT EXISTS mydatabase;
USE mydatabase;
```

通过如下语句手动创建主键表 `table2`，包含 `id` 和 `city` 两列，分别代表城市 ID 和城市名称，主键为 `id` 列，如下所示：

```SQL
CREATE TABLE `table2`
(
    `id` int(11) NOT NULL COMMENT "城市 ID",
    `city` varchar(65533) NULL COMMENT "城市名称"
)
ENGINE=OLAP
PRIMARY KEY(`id`)
DISTRIBUTED BY HASH(`id`);
```

:::note

自 2.5.7 版本起，StarRocks 支持在建表和新增分区时自动设置分桶数量 (BUCKETS)，您无需手动设置分桶数量。更多信息，请参见 [设置分桶数量](../table_design/data_distribution/Data_distribution.md#设置分桶数量)。

:::

##### 提交导入作业

通过如下语句把 `example2.json` 文件中的数据导入到 `table2` 表中：

```Bash
curl -v --location-trusted -u <username>:<password> -H "strict_mode: true" \
    -H "Expect:100-continue" \
    -H "format: json" -H "jsonpaths: [\"$.name\", \"$.code\"]" \
    -H "columns: city,tmp_id, id = tmp_id * 100" \
    -T example2.json -XPUT \
    http://<fe_host>:<fe_http_port>/api/mydatabase/table2/_stream_load
```

:::note

- 如果账号没有设置密码，这里只需要传入 `<username>:`。
- 您可以通过 [SHOW FRONTENDS](../sql-reference/sql-statements/cluster-management/nodes_processes/SHOW_FRONTENDS.md) 命令查看 FE 节点的 IP 地址和 HTTP 端口号。

:::

`example2.json` 文件中包含 `name` 和 `code` 两个键，跟 `table2` 表中的列之间的对应关系如下图所示。

![JSON 映射图](../_assets/4.2-2.png)

上图所示的对应关系描述如下：

- 提取 `example2.json` 文件中包含的 `name` 和 `code` 两个字段，按顺序依次映射到 `jsonpaths` 参数中声明的 `name` 和 `code` 两个字段。
- 提取 `jsonpaths` 参数中声明的 `name` 和 `code` 两个字段，**按顺序映射**到 `columns` 参数中声明的 `city` 和 `tmp_id` 两列。
- 提取 `columns` 参数声明中的 `city` 和 `id` 两列，**按名称映射**到 `table2` 表中的 `city` 和 `id` 两列。

:::note

上述示例中，在导入过程中先将 `example2.json` 文件中 `code` 字段对应的值乘以 100，然后再落入到 `table2` 表的 `id` 中。

:::

有关导入 JSON 数据时 `jsonpaths`、`columns` 和 StarRocks 表中的字段之间的对应关系，请参见 STREAM LOAD 文档中“[列映射](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md#列映射)”章节。

导入完成后，您可以查询 `table2` 表，验证数据导入是否成功，如下所示：

```SQL
SELECT * FROM table2;
+------+--------+
| id   | city   |
+------+--------+
| 200  | 北京    |
+------+--------+
4 rows in set (0.01 sec)
```

#### 合并 Stream Load 请求

从 v3.4.0 开始，系统支持合并多个 Stream Load 请求。

Merge Commit（合并提交）是针对 Stream Load 的优化设计，适用于高并发、小批量（从 KB 到数十 MB）的实时导入场景。在先前版本中，每个 Stream Load 请求都会生成一个事务和一个数据版本，这在高并发导入场景下会导致以下问题：

- 过多的数据版本会影响查询性能，而限制版本数量可能会引发 `too many versions` 错误。
- 通过 Compaction 合并数据版本会增加资源消耗。
- 会生成大量小文件，增加 IOPS 和 I/O 延迟。存算分离模式下，还会提高云存储成本。
- Leader FE 节点作为事务管理者可能成为单点瓶颈。

Merge Commit 将一个时间窗口内的多个并发的 Stream Load 请求合并为一个事务，从而缓解这些问题。这种方式减少了高并发请求生成的事务和版本数量，从而提升导入性能。

Merge Commit 支持同步模式和异步模式两种方式，每种方式各有优缺点，可以根据实际需求进行选择。

- **同步模式**

  服务器在合并的事务提交完成后才返回，确保导入成功且数据可见。

- **异步模式**

  服务器在接收到数据后立即返回，但不保证导入成功。

| **模式**     | **优点**                                               | **缺点**                                            |
| ------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 同步模式  | <ul><li>确保请求返回时数据已持久化且可见。</li><li>保证同一客户端的多个顺序发送的请求按序执行。</li></ul> | 单个客户端的每个请求会被阻塞，直至服务器合并窗口结束。如果窗口过大，可能会降低单个客户端的数据处理能力。 |
| 异步模式 | 客户端可以在不等待服务器关闭合并窗口的情况下发送后续导入请求，提高导入吞吐量。 | <ul><li>返回时不保证数据已持久化或可见。客户端需要在稍后验证事务状态。</li><li>不保证同一客户端的多个顺序发送的请求按序执行。</li></ul> |

##### 提交导入作业

- 使用以下命令发起一个启用了 Merge Commit 功能的 Stream Load 作业，模式为同步模式，合并窗口设置为 `5000` 毫秒，并行度设置为 `2`：

  ```Bash
  curl --location-trusted -u <username>:<password> \
      -H "Expect:100-continue" \
      -H "column_separator:," \
      -H "columns: id, name, score" \
      -H "enable_merge_commit:true" \
      -H "merge_commit_interval_ms:5000" \
      -H "merge_commit_parallel:2" \
      -T example1.csv -XPUT \
      http://<fe_host>:<fe_http_port>/api/mydatabase/table1/_stream_load
  ```

- 使用以下命令发起一个启用了 Merge Commit 功能的 Stream Load 作业，模式为异步模式，合并窗口设置为 `60000` 毫秒，并行度设置为 `2`：

  ```Bash
  curl --location-trusted -u <username>:<password> \
      -H "Expect:100-continue" \
      -H "column_separator:," \
      -H "columns: id, name, score" \
      -H "enable_merge_commit:true" \
      -H "merge_commit_async:true" \
      -H "merge_commit_interval_ms:60000" \
      -H "merge_commit_parallel:2" \
      -T example1.csv -XPUT \
      http://<fe_host>:<fe_http_port>/api/mydatabase/table1/_stream_load
  ```

:::note

- Merge Commit 仅支持单库单表的**同构**导入请求合并。“同构”是指的 Stream Load 的参数完全一致，包括：公共参数、JSON 格式参数、CSV 格式参数、`opt_properties` 以及 Merge Commit 参数。
- 导入 CSV 格式的数据时，需要确保每行数据结尾都有行分隔符，不支持 `skip_header`。
- 服务器会自动生成事务标签，手动指定标签会被忽略。
- Merge Commit 会将多个导入请求合并到一个事务中。如果某个请求存在数据质量问题，则事务中的所有请求都会失败。

:::

#### 查看 Stream Load 导入进度

导入作业结束后，StarRocks 会以 JSON 格式返回本次导入作业的结果信息，具体请参见 STREAM LOAD 文档中“[返回值](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md#返回值)”章节。

Stream Load 不支持通过 SHOW LOAD 语句查看导入作业执行情况。

#### 取消 Stream Load 作业

Stream Load 不支持手动取消导入作业。如果导入作业发生超时或者导入错误，StarRocks 会自动取消该作业。

### 参数配置

这里介绍使用 Stream Load 导入方式需要注意的一些系统参数配置。这些参数作用于所有 Stream Load 导入作业。

- `streaming_load_max_mb`：单个源数据文件的大小上限。默认文件大小上限为 10 GB。具体请参见[配置 BE（或 CN） 动态参数](../administration/management/BE_configuration.md)。

  建议一次导入的数据量不要超过 10 GB。如果数据文件的大小超过 10 GB，建议您拆分成若干小于 10 GB 的文件分次导入。如果由于业务场景需要，无法拆分数据文件，可以适当调大该参数的取值，从而提高数据文件的大小上限。

  需要注意的是，如果您调大该参数的取值，需要重启 BE（或 CN）才能生效，并且系统性能有可能会受影响，并且也会增加失败重试时的代价。

  :::note

  导入 JSON 格式的数据时，需要注意以下两点：

  - 单个 JSON 对象的大小不能超过 4 GB。如果 JSON 文件中单个 JSON 对象的大小超过 4 GB，会提示 "This parser can't support a document that big." 错误。
  - HTTP 请求中 JSON Body 的大小默认不能超过 100 MB。如果 JSON Body 的大小超过 100 MB，会提示 "The size of this batch exceed the max size [104857600] of json type data data [8617627793]. Set ignore_json_size to skip check, although it may lead huge memory consuming." 错误。为避免该报错，可以在 HTTP 请求头中添加 `"ignore_json_size:true"` 设置，忽略对 JSON Body 大小的检查。

  :::

- `stream_load_default_timeout_second`：导入作业的超时时间。默认超时时间为 600 秒。具体请参见[配置 FE 动态参数](../administration/management/FE_configuration.md)。

  如果您创建的导入作业经常发生超时，可以通过该参数适当地调大超时时间。您可以通过如下公式计算导入作业的超时时间：

  **导入作业的超时时间 > 待导入数据量/平均导入速度**

  例如，如果源数据文件的大小为 10 GB，并且当前 StarRocks 集群的平均导入速度为 100 MB/s，则超时时间应该设置为大于 100 秒。

  :::note
  
  “平均导入速度”是指目前 StarRocks 集群的平均导入速度。导入速度主要受限于集群的磁盘 I/O 及 BE（或 CN）个数。

  :::

  Stream Load 还提供 `timeout` 参数来设置当前导入作业的超时时间。具体请参见 [STREAM LOAD](../sql-reference/sql-statements/loading_unloading/STREAM_LOAD.md)。

### 使用说明

如果数据文件中某条数据记录的某个字段缺失、并且该字段对应的目标表中的列定义为 `NOT NULL`，则 StarRocks 在导入该字段时自动往目标表中对应的列填充 `NULL` 值。您也可以在导入命令中通过 `ifnull()` 函数指定您想要填充的默认值。

例如，如果上面示例里数据文件 `example2.json` 中代表城市 ID 的字段缺失、并且您想在该字段对应的目标表中的列中填充 `x`，您可以在导入命令中指定 `"columns: city, tmp_id, id = ifnull(tmp_id, 'x')"`。

## 使用 Broker Load 从本地导入

除 Stream Load 以外，您还可以通过 Broker Load 从本地导入数据。该功能自 v2.5 起支持。

Broker Load 是一种异步导入方式。提交导入作业以后，StarRocks 会异步地执行导入作业，不会直接返回作业结果，您需要手动查询作业结果。参见[查看 Broker Load 导入进度](#查看-broker-load-导入进度)。

### 使用限制

- 目前只能从单个 Broker 中导入数据，并且 Broker 版本必须为 2.5 及以后。
- 并发访问单点的 Broker 容易成为瓶颈，并发越高反而越容易造成超时、OOM 等问题。您可以通过设置 `pipeline_dop`（参见[调整查询并发度](../administration/management/resource_management/Query_management.md#调整查询并发度)）来限制 Broker Load 的并行度，对于单个 Broker 的访问并行度建议设置小于 `16`。

### 操作示例

Broker Load 支持导入单个数据文件到单张表、导入多个数据文件到单张表、以及导入多个数据文件分别到多张表。这里以导入多个数据文件到单张表为例。

注意在 StarRocks 中，部分文字是 SQL 语言的保留关键字，不能直接用于 SQL 语句。如果想在 SQL 语句中使用这些保留关键字，必须用反引号 (`) 包裹起来。参见[关键字](../sql-reference/sql-statements/keywords.md)。

#### 数据样例

以 CSV 格式的数据为例，登录本地文件系统，在指定路径（假设为 `/home/disk1/business/`）下创建两个 CSV 格式的数据文件，`file1.csv` 和 `file2.csv`。两个数据文件都包含三列，分别代表用户 ID、用户姓名和用户得分，如下所示：

- `file1.csv`

  ```Plain
  1,Lily,21
  2,Rose,22
  3,Alice,23
  4,Julia,24
  ```

- `file2.csv`

  ```Plain
  5,Tony,25
  6,Adam,26
  7,Allen,27
  8,Jacky,28
  ```

#### 建库建表

通过如下语句创建数据库、并切换至该数据库：

```SQL
CREATE DATABASE IF NOT EXISTS mydatabase;
USE mydatabase;
```

通过如下语句手动创建主键表 `mytable`，包含 `id`、`name` 和 `score` 三列，分别代表用户 ID、用户姓名和用户得分，主键为 `id` 列，如下所示：

```SQL
DROP TABLE IF EXISTS `mytable`

CREATE TABLE `mytable`
(
    `id` int(11) NOT NULL COMMENT "用户 ID",
    `name` varchar(65533) NULL DEFAULT "" COMMENT "用户姓名",
    `score` int(11) NOT NULL DEFAULT "0" COMMENT "用户得分"
)
ENGINE=OLAP
PRIMARY KEY(`id`)
DISTRIBUTED BY HASH(`id`)
PROPERTIES("replication_num"="1");
```

#### 提交导入作业

通过如下语句，把本地文件系统的 `/home/disk1/business/` 路径下所有数据文件（`file1.csv` 和 `file2.csv`）的数据导入到目标表 `mytable`：

```SQL
LOAD LABEL mydatabase.label_local
(
    DATA INFILE("file:///home/disk1/business/csv/*")
    INTO TABLE mytable
    COLUMNS TERMINATED BY ","
    (id, name, score)
)
WITH BROKER "sole_broker"
PROPERTIES
(
    "timeout" = "3600"
);
```

导入语句包含四个部分：

- `LABEL`：导入作业的标签，字符串类型，可用于查询导入作业的状态。
- `LOAD` 声明：包括源数据文件所在的 URI、源数据文件的格式、以及目标表的名称等作业描述信息。
- `BROKER`：Broker 的名称。
- `PROPERTIES`：用于指定超时时间等可选的作业属性。

有关详细的语法和参数说明，参见 [BROKER LOAD](../sql-reference/sql-statements/loading_unloading/BROKER_LOAD.md)。

#### 查看 Broker Load 导入进度

在 v3.0 及以前版本，您需要通过 [SHOW LOAD](../sql-reference/sql-statements/loading_unloading/SHOW_LOAD.md) 语句或者 curl 命令来查看导入作业的进度。

在 v3.1 及以后版本，您可以通过 [`information_schema.loads`](../sql-reference/information_schema/loads.md) 视图来查看 Broker Load 作业的进度：

```SQL
SELECT * FROM information_schema.loads;
```

如果您提交了多个导入作业，您可以通过 `LABEL` 过滤出想要查看的作业。例如：

```SQL
SELECT * FROM information_schema.loads WHERE LABEL = 'label_local';
```

确认导入作业完成后，您可以从表内查询数据，验证数据导入是否成功。例如：

```SQL
SELECT * FROM mytable;
+------+-------+-------+
| id   | name  | score |
+------+-------+-------+
|    3 | Alice |    23 |
|    5 | Tony  |    25 |
|    6 | Adam  |    26 |
|    1 | Lily  |    21 |
|    2 | Rose  |    22 |
|    4 | Julia |    24 |
|    7 | Allen |    27 |
|    8 | Jacky |    28 |
+------+-------+-------+
8 rows in set (0.07 sec)
```

#### 取消 Broker Load 作业

当导入作业状态不为 **CANCELLED** 或 **FINISHED** 时，可以通过 [CANCEL LOAD](../sql-reference/sql-statements/loading_unloading/CANCEL_LOAD.md) 语句来取消该导入作业。

例如，可以通过以下语句，撤销 `mydatabase` 数据库中标签为 `label_local` 的导入作业：

```SQL
CANCEL LOAD
FROM mydatabase
WHERE LABEL = "label_local";
```

## 使用 Broker Load 从 NAS 导入

从 NAS 导入数据时，有两种方法：

- 把 NAS 当做本地文件系统，然后按照前面“[使用 Broker Load 从本地导入](#使用-broker-load-从本地导入)”里介绍的方法来实现导入。
- 【推荐】把 NAS 作为云存储设备，这样无需部署 Broker 即可通过 Broker Load 实现导入。

本小节主要介绍这种方法。具体操作步骤如下：

1. 把 NAS 挂载到所有的 BE（或 CN）、FE 节点，同时保证所有节点的挂载路径完全一致。这样，所有 BE（或 CN）可以像访问 BE（或 CN）自己的本地文件一样访问 NAS。

2. 使用 Broker Load 导入数据。例如：

   ```SQL
   LOAD LABEL test_db.label_nas
   (
       DATA INFILE("file:///home/disk1/sr/*")
       INTO TABLE mytable
       COLUMNS TERMINATED BY ","
   )
   WITH BROKER
   PROPERTIES
   (
       "timeout" = "3600"
   );
   ```

   导入语句包含四个部分：

   - `LABEL`：导入作业的标签，字符串类型，可用于查询导入作业的状态。
   - `LOAD` 声明：包括源数据文件所在的 URI、源数据文件的格式、以及目标表的名称等作业描述信息。注意，`DATA INFILE` 这里用于指定挂载路径，如上面示例所示，`file:///` 为前缀，`/home/disk1/sr` 为挂载路径。
   - `BROKER`：无需指定。
   - `PROPERTIES`：用于指定超时时间等可选的作业属性。

   有关详细的语法和参数说明，参见 [BROKER LOAD](../sql-reference/sql-statements/loading_unloading/BROKER_LOAD.md)。

提交导入作业后，您可以查看导入进度、或者取消导入作业。具体操作参见本文“[查看 Broker Load 导入进度](#查看-broker-load-导入进度)”和“[取消 Broker Load 作业](#取消-broker-load-作业)中的介绍。
