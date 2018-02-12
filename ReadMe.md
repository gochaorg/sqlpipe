Текущее состояние - **В разработе**

# SqlPipe: command-line SQL Server backup utility #

Утилита для создания резервных копий в STDOUT/STDIN

## Использование ##

Программу необходимо запускать от администратора ОС с привилегией создания резервных копий `sysdba`

Синтаксис

	синтаксис ::= sqlpipe <действие> { <действие> }
	<действие> ::= <имя действия> | <параметры действия>
    <имя действия> ::= backup | restore
    <параметры действия> ::= -sql       <Текст sql команды>
                           | -sqlenv    <Имя переменной содержащей sql команду>
                           | -conn      <Строка соединения с СУБД>
                           | -connenv   <Имя переменной содерж. строку соединения>
                           | -login     <Логин СУБД>
                           | -loginenv  <Имя переменной содерж. Логин СУБД>
                           | -passwd    <Пароль СУБД>
                           | -passwdenv <Имя переменной содерж. Пароль СУБД>

`Текст sql команды` - В данном случаи это SQL команда содержащая инструкции резервного копирования.
Например такая:

	BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '${guid}'

`${guid}` - Подстановочное значение куда при выполнении будет подставлен сгенерированый GUID виртуального устройства.

При каждом выполнении GUID будет новый, в специфике работы VDI интерфейса считается нормальной практикой.

## Примеры ##

**Пример 1**: Резервное копирование от Администратора ОС

<pre><code><span style="color:#888888">user@TESTVM01 C:\Users\user\Documents\code\sqlpipe\Debug
$</span> SqlPipe.exe backup -conn "Provider=SQLOLEDB; Data Source=.; Initial Catalog=master; Integrated Security=SSPI;" -sql "BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '<span style="color:#500000">${guid}</span>'" > C:\backup\test04.bak
<span style="color:#404040">passed sql command: BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '<span style="color:#500000">${guid}</span>'
action: backup
created guid: <span style="color:#500000">{3222731B-AC0D-4CF6-A89C-B018A04658DA}</span>
prepared sql: BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '<span style="color:#500000">{3222731B-AC0D-4CF6-A89C-B018A04658DA}</span>'
target: stdout
connect: Provider=SQLOLEDB; Data Source=.; Initial Catalog=master; Integrated Security=SSPI;
1664512 bytes written</span></code></pre>

В примере указана SQL команда `BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '${guid}'`.
При выполнеии будет создан GUID равный `{3222731B-AC0D-4CF6-A89C-B018A04658DA}`, а сама команда будет транслирована в следующую `BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '{3222731B-AC0D-4CF6-A89C-B018A04658DA}'`

**Пример 2**: Резервное копирование от Адмнистратора ОС и специального пользователя `backup`

	user@TESTVM01 C:\Users\user\Documents\code\sqlpipe\Debug
	$ SqlPipe.exe backup -conn "Provider=SQLOLEDB; Data Source=.; Initial Catalog=master;" -login backup -passwd backuppswd -sql "BACKUP DATABASE [test] TO VIRTUAL_DEVICE = '${guid}'" > C:\backup\test04.ba


## Спецификация VDI ##

Uses SQL Server's Virtual Backup Device Interface as detailed in the [SQL Server 2005 Virtual Backup Device Interface (VDI) Specification](http://www.microsoft.com/downloads/en/details.aspx?familyid=416f8a51-65a3-4e8e-a4c8-adfe15e850fc).
