/**
 ******************************************************************************
 *
 * @file       logfile.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013
 * @brief      Plugin for generating a logfile
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "logfile.h"
#include <QDebug>
#include <QtGlobal>
#include <QTextStream>
#include <QMessageBox>

#include <coreplugin/coreconstants.h>

LogFile::LogFile(QObject *parent) :
    QIODevice(parent),
    timestampBufferIdx(0)
{
    connect(&timer, SIGNAL(timeout()), this, SLOT(timerFired()));
}

/**
 * Opens the logfile QIODevice and the underlying logfile. In case
 * we want to save the logfile, we open in WriteOnly. In case we
 * want to read the logfile, we open in ReadOnly.
 */
bool LogFile::open(OpenMode mode) {

    // start a timer for playback
    myTime.restart();
    if (file.isOpen()) {
        // We end up here when doing a replay, because the connection
        // manager will also try to open the QIODevice, even though we just
        // opened it after selecting the file, which happens before the
        // connection manager call...
        return true;
    }

    //Open file as either WriteOnly, or ReadOnly, depending on `mode` parameter
    if(file.open(mode) == false)
    {
        qDebug() << "Unable to open " << file.fileName() << " for logging";
        return false;
    }

    // TODO: Write a header at the beginng describing objects so that in future
    // they can be read back if ID's change

    /*This addresses part of the TODO. It writes the git hash to the beginning of the file. This will
     * not protect against data losses due to UAVO that have changed when there is no commit to public
     * git, or to commits that are based off of branches that have since been pruned. As such, this
     * can only be seen as a temporary fix.
     */
    if(mode==QIODevice::WriteOnly)
    {
        QString gitHash = QString::fromLatin1(Core::Constants::GCS_REVISION_STR);
        // UAVOSHA1_STR looks something like: "{ 0xbd,0xfc,0x47,0x16,0x59,0xb9,0x08,0x18,0x1c,0x82,0x5e,0x3f,0xe1,0x1a,0x77,0x7f,0x4e,0x06,0xea,0x7c }"
        // This string needs to be reduced to just the hex letters, so in the example we need: bdfc471659b908181c825e3fe11a777f4e06ea7c
        QString uavoHash = QString::fromLatin1(Core::Constants::UAVOSHA1_STR).replace("\"{ ", "").replace(" }\"", "").replace(",", "").replace("0x", "");
        QTextStream out(&file);

        out << "dRonin git hash:\n" <<  gitHash << "\n" << uavoHash << "\n##\n";
    }
    else if(mode == QIODevice::ReadOnly)
    {
        file.readLine(); //Read first line of log file. This assumes that the logfile is of the new format.
        QString logGitHashString=file.readLine().trimmed(); //Read second line of log file. This assumes that the logfile is of the new format.
        QString logUAVOHashString=file.readLine().trimmed(); //Read third line of log file. This assumes that the logfile is of the new format.
        QString gitHash = QString::fromLatin1(Core::Constants::GCS_REVISION_STR);
        QString uavoHash = QString::fromLatin1(Core::Constants::UAVOSHA1_STR).replace("\"{ ", "").replace(" }\"", "").replace(",", "").replace("0x", ""); // See comment above for necessity for string replacements

        if(logUAVOHashString != uavoHash){
            QMessageBox msgBox;
            msgBox.setText("Likely log file incompatibility.");
            msgBox.setInformativeText(QString("The log file was made with branch %1, UAVO hash %2. GCS will attempt to play the file.").arg(logGitHashString).arg(logUAVOHashString));
            msgBox.exec();
        }
        else if(logGitHashString != gitHash){
            QMessageBox msgBox;
            msgBox.setText("Possible log file incompatibility.");
            msgBox.setInformativeText(QString("The log file was made with branch %1. GCS will attempt to play the file.").arg(logGitHashString));
            msgBox.exec();
        }

        QString tmpLine=file.readLine(); //Look for the header/body separation string.
        int cnt=0;
        while (tmpLine!="##\n" && cnt < 10 && !file.atEnd()){
            tmpLine=file.readLine().trimmed();
            cnt++;
        }

        //Check if we reached the end of the file before finding the separation string
        if (cnt >=10 || file.atEnd()){
            QMessageBox msgBox;
            msgBox.setText("Corrupted file.");
            msgBox.setInformativeText("GCS cannot find the separation byte. GCS will attempt to play the file."); //<--TODO: add hyperlink to webpage with better description.
            msgBox.exec();

            //Since we could not find the file separator, we need to return to the beginning of the file
            file.seek(0);
        }

    }
    else
    {
        qDebug()<< "Logging read/write mode incorrectly set.";
    }

    // Must call parent function for QIODevice to pass calls to writeData
    // We always open ReadWrite, because otherwise we will get tons of warnings
    // during a logfile replay. Read nature is checked upon write ops below.
    QIODevice::open(QIODevice::ReadWrite);

    return true;
}

void LogFile::close()
{
    emit aboutToClose();

    if (timer.isActive())
        timer.stop();
    file.close();
    QIODevice::close();
}

qint64 LogFile::writeData(const char * data, qint64 dataSize) {
    if (!file.isWritable())
        return dataSize;

    quint32 timeStamp = myTime.elapsed();

    file.write((char *) &timeStamp,sizeof(timeStamp));
    file.write((char *) &dataSize, sizeof(dataSize));

    qint64 written = file.write(data, dataSize);
    if(written != -1)
        emit bytesWritten(written);

    return dataSize;
}

qint64 LogFile::readData(char * data, qint64 maxSize) {
    QMutexLocker locker(&mutex);
    qint64 toRead = qMin(maxSize,(qint64)dataBuffer.size());
    memcpy(data,dataBuffer.data(),toRead);
    dataBuffer.remove(0,toRead);
    return toRead;
}

qint64 LogFile::bytesAvailable() const
{
    return dataBuffer.size();
}

void LogFile::timerFired()
{
    qint64 dataSize;

    if(file.bytesAvailable() > 4)
    {

        int time;
        time = myTime.elapsed();

        //Read packets
        while ((lastPlayTime + ((time - lastPlayTimeOffset)* playbackSpeed) > (lastTimeStamp-firstTimestamp)))
        {
            lastPlayTime += ((time - lastPlayTimeOffset)* playbackSpeed);
            if(file.bytesAvailable() < 4) {
                stopReplay();
                return;
            }

            file.seek(lastTimeStampPos+sizeof(lastTimeStamp));

            file.read((char *) &dataSize, sizeof(dataSize));

            if (dataSize<1 || dataSize>(1024*1024)) {
                qDebug() << "Error: Logfile corrupted! Unlikely packet size: " << dataSize << "\n";
                stopReplay();
                return;
            }
            if(file.bytesAvailable() < dataSize) {
                stopReplay();
                return;
            }

            mutex.lock();
            dataBuffer.append(file.read(dataSize));
            mutex.unlock();
            emit readyRead();

            if(file.bytesAvailable() < 4) {
                stopReplay();
                return;
            }

            lastTimeStampPos = timestampPos[timestampBufferIdx];
            lastTimeStamp = timestampBuffer[timestampBufferIdx];
            timestampBufferIdx++;

            lastPlayTimeOffset = time;
            time = myTime.elapsed();

        }
    } else {
        stopReplay();
    }

}

bool LogFile::startReplay() {
    dataBuffer.clear();
    myTime.restart();
    lastPlayTimeOffset = 0;
    lastPlayTime = 0;
    playbackSpeed = 1;

    //Read all log timestamps into array
    timestampBuffer.clear(); //Save beginning of log for later use
    timestampPos.clear();
    quint64 logFileStartIdx = file.pos();
    timestampBufferIdx = 0;
    lastTimeStamp = 0;

    while (!file.atEnd()){
        qint64 dataSize;

        //Get time stamp position
        timestampPos.append(file.pos());

        //Read timestamp and logfile packet size
        file.read((char *) &lastTimeStamp, sizeof(lastTimeStamp));
        file.read((char *) &dataSize, sizeof(dataSize));

        //Check if dataSize sync bytes are correct.
        //TODO: LIKELY AS NOT, THIS WILL FAIL TO RESYNC BECAUSE THERE IS TOO LITTLE INFORMATION IN THE STRING OF SIX 0x00
        if ((dataSize & 0xFFFFFFFFFFFF0000)!=0){
            qDebug() << "Wrong sync byte. At file location 0x"  << QString("%1").arg(file.pos(),0,16) << "Got 0x" << QString("%1").arg(dataSize & 0xFFFFFFFFFFFF0000,0,16) << ", but expected 0x""00"".";
            file.seek(timestampPos.last()+1);
            timestampPos.pop_back();
            continue;
        }

        //Check if timestamps are sequential.
        if (!timestampBuffer.isEmpty() && lastTimeStamp < timestampBuffer.last()){
            QMessageBox msgBox;
            msgBox.setText("Corrupted file.");
            msgBox.setInformativeText("Timestamps are not sequential. Playback may have unexpected behavior"); //<--TODO: add hyperlink to webpage with better description.
            msgBox.exec();

            qDebug() << "Timestamp: " << timestampBuffer.last() << " " << lastTimeStamp;
        }

        timestampBuffer.append(lastTimeStamp);

        file.seek(timestampPos.last()+sizeof(lastTimeStamp)+sizeof(dataSize)+dataSize);
    }

    //Check if any timestamps were successfully read
    if (timestampBuffer.size() == 0){
        QMessageBox msgBox;
        msgBox.setText("Empty logfile.");
        msgBox.setInformativeText("No log data can be found.");
        msgBox.exec();

        stopReplay();
        return false;
    }

    //Reset to log beginning.
    file.seek(logFileStartIdx+sizeof(lastTimeStamp));
    lastTimeStampPos = timestampPos[0];
    lastTimeStamp = timestampBuffer[0];
    firstTimestamp = timestampBuffer[0];
    timestampBufferIdx = 1;

    timer.setInterval(10);
    timer.start();
    emit replayStarted();
    return true;
}

bool LogFile::stopReplay() {
    close();
    emit replayFinished();
    return true;
}

void LogFile::pauseReplay()
{
    timer.stop();
}

void LogFile::resumeReplay()
{
    lastPlayTimeOffset = myTime.elapsed();
    timer.start();
}

/**
 * @brief LogFile::setReplayTime, sets the playback time
 * @param val, the time in
 */
void LogFile::setReplayTime(double val)
{
    quint32 tmpIdx=0;
    while(timestampBuffer[tmpIdx++] <= val*1000 && tmpIdx <= timestampBufferIdx){
    }

    lastTimeStampPos=timestampPos[tmpIdx];
    lastTimeStamp=timestampBuffer[tmpIdx];
    timestampBufferIdx=tmpIdx;

    lastPlayTimeOffset = myTime.elapsed();
    lastPlayTime=lastTimeStamp;

    qDebug() << "Replaying at: " << lastTimeStamp << ", but requestion at" << val*1000;
}

