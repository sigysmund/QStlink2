/*
This file is part of QSTLink2.

    QSTLink2 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    QSTLink2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with QSTLink2.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "transferthread.h"

transferThread::transferThread(QObject *parent) :
    QThread(parent)
{
    qDebug() << "New Transfer Thread";
    this->stop = false;
}

void transferThread::run()
{
    if (this->write)
        this->send(this->filename);
    else
        this->receive(this->filename);
}
void transferThread::halt()
{
    this->stop = true;
    emit sendStatus("Aborted");
    emit sendLog("Transfer Aborted");
}

void transferThread::setParams(stlinkv2 *stlink, QString filename, bool write)
{
    this->stlink = stlink;
    this->filename = filename;
    this->write = write;
}

void transferThread::send(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical("Could not open the file.");
        return;
    }
    emit sendLock(true);
    this->stop = false;
    this->stlink->resetMCU(); // We stop the MCU
    quint32 program_size = 4; // WORD (4 bytes)
    quint32 from = this->stlink->device->flash_base;
    quint32 to = this->stlink->device->flash_base+file.size();
    qInformal() << "Writing from" << QString::number(from, 16) << "to" << QString::number(to, 16);
    QByteArray buf;
    quint32 addr, progress, oldprogress, read;
    char buf2[program_size];

    // Unlock flash
    if (!this->stlink->unlockFlash())
        return;

    this->stlink->setProgramSize(program_size);

    emit sendStatus("Erasing flash... This might take some time.");
    qInformal() << "Erasing flash... This might take some time.";
    this->stlink->eraseFlash();

    qDebug() << "\n";
    // We finally enable flash programming
    if (!this->stlink->setFlashProgramming(true))
        return;

    while(this->stlink->isBusy()) {
        usleep(100000); // 100ms
    }

    // Unlock flash again. Seems like this is mandatory.
    if (!this->stlink->unlockFlash())
        return;

    progress = 0;
    for (int i=0; i<=file.size(); i+=program_size)
    {
        buf.clear();

        if (this->stop)
            break;

        if (file.atEnd()) {
            qDebug() << "EOF";
            break;
        }
        memset(buf2, 0, program_size);
        if ((read = file.read(buf2, program_size)) <= 0)
            break;
        buf.append(buf2, read);

        addr = this->stlink->device->flash_base+i;
        this->stlink->writeMem32(addr, buf);
        oldprogress = progress;
        progress = (i*100)/file.size();
        if (progress > oldprogress) { // Push only if number has increased
            emit sendProgress(progress);
            qInformal() << "Progress:"<< QString::number(progress)+"%";
        }
        emit sendStatus("Transfered "+QString::number(i/1024)+" kilobytes out of "+QString::number(file.size()/1024));
    }
    file.close();

    emit sendProgress(100);
    emit sendStatus("Transfer done");
    emit sendLog("Transfer done");
    qInformal() << "Transfer done";

    // We disable flash programming
    if (this->stlink->setFlashProgramming(false))
        return;

    // Lock flash
    this->stlink->lockFlash();

    this->stlink->resetMCU();
    this->stlink->runMCU();

    emit sendLock(false);
}

void transferThread::receive(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadWrite)) {
        qCritical("Could not save the file.");
        return;
    }
    emit sendLock(true);
    this->stop = false;
    this->stlink->resetMCU(); // We stop the MCU
    quint32 program_size = this->stlink->device->flash_pgsize*4;
    quint32 from = this->stlink->device->flash_base;
    quint32 to = this->stlink->device->flash_base+from;
    qInformal() << "Reading from" << QString::number(from, 16) << "to" << QString::number(to, 16);
    quint32 addr, progress, oldprogress;

    progress = 0;
    for (quint32 i=0; i<this->stlink->device->flash_size; i+=program_size)
    {
        if (this->stop)
            break;
        addr = this->stlink->device->flash_base+i;
        if (this->stlink->readMem32(addr, program_size) < 0)
            break;
        file.write(this->stlink->recv_buf);
        oldprogress = progress;
        progress = (i*100)/this->stlink->device->flash_size;
        if (progress > oldprogress) { // Push only if number has increased
            emit sendProgress(progress);
            qInformal() << "Progress:"<< QString::number(progress)+"%";
        }
        emit sendStatus("Transfered "+QString::number(i/1024)+" kilobytes out of "+QString::number(this->stlink->device->flash_size/1024));
    }
    file.close();
    emit sendProgress(100);
    emit sendStatus("Transfer done");
    qInformal() << "Transfer done";
    this->stlink->runMCU();
    emit sendLock(false);
}