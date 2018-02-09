Текущее состояние - **В разработе**

# SqlPipe: command-line SQL Server backup utility #

Утилита для создания резервных копий в STDOUT/STDIN

## Использование ##

    sqlpipe <action> {<action_params>} 
    <action> ::= backup | restore
    <action_params> ::= <sql_backuprestore_query> | <sql_backuprestore_envvar>
    <sql_backuprestore_query> ::= -sql sql_backup_command
    <sql_backuprestore_envvar> ::= -sqlenv env_var

## Примеры ##

_документация в разработке_

## Implementation details ##

Uses SQL Server's Virtual Backup Device Interface as detailed in the [SQL Server 2005 Virtual Backup Device Interface (VDI) Specification](http://www.microsoft.com/downloads/en/details.aspx?familyid=416f8a51-65a3-4e8e-a4c8-adfe15e850fc). It's not a million miles away from the `simple.cpp` example there.
