/*
    Copyright © 2015-2018 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QThread>

#include <cassert>
#include <sodium.h>

#include "profile.h"
#include "profilelocker.h"
#include "settings.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/core/corefile.h"
#include "src/net/avatarbroadcaster.h"
#include "src/nexus.h"
#include "src/widget/gui.h"
#include "src/widget/tool/identicon.h"
#include "src/widget/widget.h"

/**
 * @class Profile
 * @brief Manages user profiles.
 *
 * @var bool Profile::newProfile
 * @brief True if this is a newly created profile, with no .tox save file yet.
 *
 * @var bool Profile::isRemoved
 * @brief True if the profile has been removed by remove().
 */

QStringList Profile::profiles;

void Profile::initCore(const QByteArray& toxsave, ICoreSettings& s, bool isNewProfile)
{
    if (toxsave.isEmpty() && !isNewProfile) {
        qCritical() << "Existing toxsave is empty";
        emit failedToStart();
    }

    if (!toxsave.isEmpty() && isNewProfile) {
        qCritical() << "New profile has toxsave data";
        emit failedToStart();
    }

    Core::ToxCoreErrors err;
    core = Core::makeToxCore(toxsave, &s, &err);
    if (!core) {
        switch (err) {
        case Core::ToxCoreErrors::BAD_PROXY:
            emit badProxy();
            break;
        case Core::ToxCoreErrors::ERROR_ALLOC:
        case Core::ToxCoreErrors::FAILED_TO_START:
        case Core::ToxCoreErrors::INVALID_SAVE:
        default:
            emit failedToStart();
        }

        qDebug() << "failed to start ToxCore";
        return;
    }

    if (isNewProfile) {
        core->setStatusMessage(tr("Toxing on qTox"));
        core->setUsername(name);
    }

    // save tox file when Core requests it
    connect(core.get(), &Core::saveRequest, this, &Profile::onSaveToxSave);
    // react to avatar changes
    connect(core.get(), &Core::friendAvatarRemoved, this, &Profile::removeAvatar);
    connect(core.get(), &Core::friendAvatarChanged, this, &Profile::setFriendAvatar);
    connect(core.get(), &Core::fileAvatarOfferReceived, this, &Profile::onAvatarOfferReceived,
            Qt::ConnectionType::QueuedConnection);
}

Profile::Profile(QString name, const QString& password, bool isNewProfile,
                 const QByteArray& toxsave, std::unique_ptr<ToxEncrypt> passkey)
    : name{name}
    , passkey{std::move(passkey)}
    , isRemoved{false}
    , encrypted{this->passkey != nullptr}
{
    Settings& s = Settings::getInstance();
    s.setCurrentProfile(name);
    s.saveGlobal();
    s.loadPersonal(name, this->passkey.get());
    initCore(toxsave, s, isNewProfile);

    const ToxId& selfId = core->getSelfId();
    loadDatabase(selfId, password);
}

/**
 * @brief Locks and loads an existing profile and creates the associate Core* instance.
 * @param name Profile name.
 * @param password Profile password.
 * @return Returns a nullptr on error. Profile pointer otherwise.
 *
 * @example If the profile is already in use return nullptr.
 */
Profile* Profile::loadProfile(QString name, const QString& password)
{
    if (ProfileLocker::hasLock()) {
        qCritical() << "Tried to load profile " << name << ", but another profile is already locked!";
        return nullptr;
    }

    if (!ProfileLocker::lock(name)) {
        qWarning() << "Failed to lock profile " << name;
        return nullptr;
    }

    std::unique_ptr<ToxEncrypt> tmpKey = nullptr;
    QByteArray data = QByteArray();
    Profile* p = nullptr;
    qint64 fileSize = 0;

    QString path = Settings::getInstance().getSettingsDirPath() + name + ".tox";
    QFile saveFile(path);
    qDebug() << "Loading tox save " << path;

    if (!saveFile.exists()) {
        qWarning() << "The tox save file " << path << " was not found";
        goto fail;
    }

    if (!saveFile.open(QIODevice::ReadOnly)) {
        qCritical() << "The tox save file " << path << " couldn't' be opened";
        goto fail;
    }

    fileSize = saveFile.size();
    if (fileSize <= 0) {
        qWarning() << "The tox save file" << path << " is empty!";
        goto fail;
    }

    data = saveFile.readAll();
    if (ToxEncrypt::isEncrypted(data)) {
        if (password.isEmpty()) {
            qCritical() << "The tox save file is encrypted, but we don't have a password!";
            goto fail;
        }

        tmpKey = ToxEncrypt::makeToxEncrypt(password, data);
        if (!tmpKey) {
            qCritical() << "Failed to derive key of the tox save file";
            goto fail;
        }

        data = tmpKey->decrypt(data);
        if (data.isEmpty()) {
            qCritical() << "Failed to decrypt the tox save file";
            goto fail;
        }
    } else {
        if (!password.isEmpty()) {
            qWarning() << "We have a password, but the tox save file is not encrypted";
        }
    }

    saveFile.close();
    p = new Profile(name, password, false, data, std::move(tmpKey));
    return p;

// cleanup in case of error
fail:
    saveFile.close();
    ProfileLocker::unlock();
    return nullptr;
}

/**
 * @brief Creates a new profile and the associated Core* instance.
 * @param name Username.
 * @param password If password is not empty, the profile will be encrypted.
 * @return Returns a nullptr on error. Profile pointer otherwise.
 *
 * @note If the profile is already in use return nullptr.
 */
Profile* Profile::createProfile(QString name, QString password)
{
    std::unique_ptr<ToxEncrypt> tmpKey;
    if (!password.isEmpty()) {
        tmpKey = ToxEncrypt::makeToxEncrypt(password);
        if (!tmpKey) {
            qCritical() << "Failed to derive key for the tox save";
            return nullptr;
        }
    }

    if (ProfileLocker::hasLock()) {
        qCritical() << "Tried to create profile " << name << ", but another profile is already locked!";
        return nullptr;
    }

    if (exists(name)) {
        qCritical() << "Tried to create profile " << name << ", but it already exists!";
        return nullptr;
    }

    if (!ProfileLocker::lock(name)) {
        qWarning() << "Failed to lock profile " << name;
        return nullptr;
    }

    Settings::getInstance().createPersonal(name);
    Profile* p = new Profile(name, password, true, QByteArray(), std::move(tmpKey));
    return p;
}

Profile::~Profile()
{
    if (isRemoved) {
        return;
    }

    onSaveToxSave();
    Settings::getInstance().savePersonal(this);
    Settings::getInstance().sync();
    ProfileLocker::assertLock();
    assert(ProfileLocker::getCurLockName() == name);
    ProfileLocker::unlock();
}

/**
 * @brief Lists all the files in the config dir with a given extension
 * @param extension Raw extension, e.g. "jpeg" not ".jpeg".
 * @return Vector of filenames.
 */
QStringList Profile::getFilesByExt(QString extension)
{
    QDir dir(Settings::getInstance().getSettingsDirPath());
    QStringList out;
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setNameFilters(QStringList("*." + extension));
    QFileInfoList list = dir.entryInfoList();
    out.reserve(list.size());
    for (QFileInfo file : list) {
        out += file.completeBaseName();
    }

    return out;
}

/**
 * @brief Scan for profile, automatically importing them if needed.
 * @warning NOT thread-safe.
 */
void Profile::scanProfiles()
{
    profiles.clear();
    QStringList toxfiles = getFilesByExt("tox"), inifiles = getFilesByExt("ini");
    for (QString toxfile : toxfiles) {
        if (!inifiles.contains(toxfile)) {
            Settings::getInstance().createPersonal(toxfile);
        }

        profiles.append(toxfile);
    }
}

QStringList Profile::getProfiles()
{
    return profiles;
}

Core* Profile::getCore()
{
    // TODO(sudden6): this is evil
    return core.get();
}

QString Profile::getName() const
{
    return name;
}

/**
 * @brief Starts the Core thread
 */
void Profile::startCore()
{
    core->start();

    const ToxId& selfId = core->getSelfId();
    const ToxPk& selfPk = selfId.getPublicKey();
    const QByteArray data = loadAvatarData(selfPk);
    if (data.isEmpty()) {
        qDebug() << "Self avatar not found, will broadcast empty avatar to friends";
    }
    // TODO(sudden6): moved here, because it crashes in the constructor
    // reason: Core::getInstance() returns nullptr, because it's not yet initialized
    // solution: kill Core::getInstance
    setAvatar(data);
}

/**
 * @brief Saves the profile's .tox save, encrypted if needed.
 * @warning Invalid on deleted profiles.
 */
void Profile::onSaveToxSave()
{
    QByteArray data = core->getToxSaveData();
    assert(data.size());
    saveToxSave(data);
}

// TODO(sudden6): handle this better maybe?
void Profile::onAvatarOfferReceived(uint32_t friendId, uint32_t fileId, const QByteArray& avatarHash)
{
    // accept if we don't have it already
    const bool accept = getAvatarHash(core->getFriendPublicKey(friendId)) != avatarHash;
    core->getCoreFile()->handleAvatarOffer(friendId, fileId, accept);
}

/**
 * @brief Write the .tox save, encrypted if needed.
 * @param data Byte array of profile save.
 * @return true if successfully saved, false otherwise
 * @warning Invalid on deleted profiles.
 */
bool Profile::saveToxSave(QByteArray data)
{
    assert(!isRemoved);
    ProfileLocker::assertLock();
    assert(ProfileLocker::getCurLockName() == name);

    QString path = Settings::getInstance().getSettingsDirPath() + name + ".tox";
    qDebug() << "Saving tox save to " << path;
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qCritical() << "Tox save file " << path << " couldn't be opened";
        return false;
    }

    if (encrypted) {
        data = passkey->encrypt(data);
        if (data.isEmpty()) {
            qCritical() << "Failed to encrypt, can't save!";
            saveFile.cancelWriting();
            return false;
        }
    }

    saveFile.write(data);

    // check if everything got written
    if (saveFile.flush()) {
        saveFile.commit();
    } else {
        saveFile.cancelWriting();
        qCritical() << "Failed to write, can't save!";
        return false;
    }
    return true;
}

/**
 * @brief Gets the path of the avatar file cached by this profile and corresponding to this owner
 * ID.
 * @param owner Path to avatar of friend with this PK will returned.
 * @param forceUnencrypted If true, return the path to the plaintext file even if this is an
 * encrypted profile.
 * @return Path to the avatar.
 */
QString Profile::avatarPath(const ToxPk& owner, bool forceUnencrypted)
{
    const QString ownerStr = owner.toString();
    if (!encrypted || forceUnencrypted) {
        return Settings::getInstance().getSettingsDirPath() + "avatars/" + ownerStr + ".png";
    }

    QByteArray idData = ownerStr.toUtf8();
    QByteArray pubkeyData = core->getSelfId().getPublicKey().getByteArray();
    constexpr int hashSize = TOX_PUBLIC_KEY_SIZE;
    static_assert(hashSize >= crypto_generichash_BYTES_MIN && hashSize <= crypto_generichash_BYTES_MAX,
                  "Hash size not supported by libsodium");
    static_assert(hashSize >= crypto_generichash_KEYBYTES_MIN
                      && hashSize <= crypto_generichash_KEYBYTES_MAX,
                  "Key size not supported by libsodium");
    QByteArray hash(hashSize, 0);
    crypto_generichash((uint8_t*)hash.data(), hashSize, (uint8_t*)idData.data(), idData.size(),
                       (uint8_t*)pubkeyData.data(), pubkeyData.size());
    return Settings::getInstance().getSettingsDirPath() + "avatars/" + hash.toHex().toUpper() + ".png";
}

/**
 * @brief Get our avatar from cache.
 * @return Avatar as QPixmap.
 */
QPixmap Profile::loadAvatar()
{
    return loadAvatar(core->getSelfId().getPublicKey());
}

/**
 * @brief Get a contact's avatar from cache.
 * @param owner Friend PK to load avatar.
 * @return Avatar as QPixmap.
 */
QPixmap Profile::loadAvatar(const ToxPk& owner)
{
    QPixmap pic;
    if (Settings::getInstance().getShowIdenticons()) {

        const QByteArray avatarData = loadAvatarData(owner);
        if (avatarData.isEmpty()) {
            pic = QPixmap::fromImage(Identicon(owner.getByteArray()).toImage(16));
        } else {
            pic.loadFromData(avatarData);
        }

    } else {
        pic.loadFromData(loadAvatarData(owner));
    }

    return pic;
}

/**
 * @brief Get a contact's avatar from cache.
 * @param owner Friend PK to load avatar.
 * @return Avatar as QByteArray.
 */
QByteArray Profile::loadAvatarData(const ToxPk& owner)
{
    QString path = avatarPath(owner);
    bool avatarEncrypted = encrypted;
    // If the encrypted avatar isn't found, try loading the unencrypted one for the same ID
    if (avatarEncrypted && !QFile::exists(path)) {
        avatarEncrypted = false;
        path = avatarPath(owner, true);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QByteArray pic = file.readAll();
    if (avatarEncrypted && !pic.isEmpty()) {
        pic = passkey->decrypt(pic);
        if (pic.isEmpty()) {
            qWarning() << "Failed to decrypt avatar at" << path;
        }
    }

    return pic;
}

void Profile::loadDatabase(const ToxId& id, QString password)
{
    if (isRemoved) {
        qDebug() << "Can't load database of removed profile";
        return;
    }

    QByteArray salt = id.getPublicKey().getByteArray();
    if (salt.size() != TOX_PASS_SALT_LENGTH) {
        qWarning() << "Couldn't compute salt from public key" << name;
        GUI::showError(QObject::tr("Error"),
                       QObject::tr("qTox couldn't open your chat logs, they will be disabled."));
    }
    // At this point it's too early to load the personal settings (Nexus will do it), so we always
    // load
    // the history, and if it fails we can't change the setting now, but we keep a nullptr
    database = std::make_shared<RawDatabase>(getDbPath(name), password, salt);
    if (database && database->isOpen()) {
        history.reset(new History(database));
    } else {
        qWarning() << "Failed to open database for profile" << name;
        GUI::showError(QObject::tr("Error"),
                       QObject::tr("qTox couldn't open your chat logs, they will be disabled."));
    }
}

/**
 * @brief Sets our own avatar
 * @param pic Picture to use as avatar, if empty an Identicon will be used depending on settings
 */
void Profile::setAvatar(QByteArray pic)
{
    QPixmap pixmap;
    QByteArray avatarData;
    const ToxPk& selfPk = core->getSelfPublicKey();
    if (!pic.isEmpty()) {
        pixmap.loadFromData(pic);
        avatarData = pic;
    } else {
        if (Settings::getInstance().getShowIdenticons()) {
            const QImage identicon = Identicon(selfPk.getByteArray()).toImage(32);
            pixmap = QPixmap::fromImage(identicon);

        } else {
            pixmap.load(":/img/contact_dark.svg");
        }
    }

    saveAvatar(selfPk, avatarData);

    emit selfAvatarChanged(pixmap);
    AvatarBroadcaster::setAvatar(avatarData);
    AvatarBroadcaster::enableAutoBroadcast();
}


/**
 * @brief Sets a friends avatar
 * @param pic Picture to use as avatar, if empty an Identicon will be used depending on settings
 * @param owner pk of friend
 */
void Profile::setFriendAvatar(const ToxPk& owner, QByteArray pic)
{
    QPixmap pixmap;
    QByteArray avatarData;
    if (!pic.isEmpty()) {
        pixmap.loadFromData(pic);
        avatarData = pic;
        emit friendAvatarSet(owner, pixmap);
    } else if (Settings::getInstance().getShowIdenticons()) {
        const QImage identicon = Identicon(owner.getByteArray()).toImage(32);
        pixmap = QPixmap::fromImage(identicon);
        emit friendAvatarSet(owner, pixmap);
    } else {
        pixmap.load(":/img/contact_dark.svg");
        emit friendAvatarRemoved(owner);
    }
    friendAvatarChanged(owner, pixmap);
    saveAvatar(owner, avatarData);
}

/**
 * @brief Adds history message about friendship request attempt if history is enabled
 * @param friendPk Pk of a friend which request is destined to
 * @param message Friendship request message
 */
void Profile::onRequestSent(const ToxPk& friendPk, const QString& message)
{
    if (!isHistoryEnabled()) {
        return;
    }

    const QString pkStr = friendPk.toString();
    const QString inviteStr = Core::tr("/me offers friendship, \"%1\"").arg(message);
    const QString selfStr = core->getSelfPublicKey().toString();
    const QDateTime datetime = QDateTime::currentDateTime();
    const QString name = core->getUsername();
    history->addNewMessage(pkStr, inviteStr, selfStr, datetime, true, name);
}

/**
 * @brief Save an avatar to cache.
 * @param pic Picture to save.
 * @param owner PK of avatar owner.
 */
void Profile::saveAvatar(const ToxPk& owner, const QByteArray& avatar)
{
    const bool needEncrypt = encrypted && !avatar.isEmpty();
    const QByteArray& pic = needEncrypt ? passkey->encrypt(avatar) : avatar;

    QString path = avatarPath(owner);
    QDir(Settings::getInstance().getSettingsDirPath()).mkdir("avatars");
    if (pic.isEmpty()) {
        QFile::remove(path);
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Tox avatar " << path << " couldn't be saved";
            return;
        }
        file.write(pic);
        file.commit();
    }
}

/**
 * @brief Get the tox hash of a cached avatar.
 * @param owner Friend PK to get hash.
 * @return Avatar tox hash.
 */
QByteArray Profile::getAvatarHash(const ToxPk& owner)
{
    QByteArray pic = loadAvatarData(owner);
    QByteArray avatarHash(TOX_HASH_LENGTH, 0);
    tox_hash((uint8_t*)avatarHash.data(), (uint8_t*)pic.data(), pic.size());
    return avatarHash;
}

/**
 * @brief Removes our own avatar.
 */
void Profile::removeSelfAvatar()
{
    removeAvatar(core->getSelfId().getPublicKey());
}

/**
 * @brief Removes friend avatar.
 */
void Profile::removeFriendAvatar(const ToxPk& owner)
{
    removeAvatar(owner);
}

/**
 * @brief Checks that the history is enabled in the settings, and loaded successfully for this
 * profile.
 * @return True if enabled, false otherwise.
 */
bool Profile::isHistoryEnabled()
{
    return Settings::getInstance().getEnableLogging() && history;
}

/**
 * @brief Get chat history.
 * @return May return a nullptr if the history failed to load.
 */
History* Profile::getHistory()
{
    return history.get();
}

/**
 * @brief Removes a cached avatar.
 * @param owner Friend PK whose avater to delete.
 */
void Profile::removeAvatar(const ToxPk& owner)
{
    QFile::remove(avatarPath(owner));
    if (owner == core->getSelfId().getPublicKey()) {
        setAvatar({});
    } else {
        setFriendAvatar(owner, {});
    }
}

bool Profile::exists(QString name)
{
    QString path = Settings::getInstance().getSettingsDirPath() + name;
    return QFile::exists(path + ".tox");
}

/**
 * @brief Checks, if profile has a password.
 * @return True if we have a password set (doesn't check the actual file on disk).
 */
bool Profile::isEncrypted() const
{
    return encrypted;
}

/**
 * @brief Checks if profile is encrypted.
 * @note Checks the actual file on disk.
 * @param name Profile name.
 * @return True if profile is encrypted, false otherwise.
 */
bool Profile::isEncrypted(QString name)
{
    uint8_t data[TOX_PASS_ENCRYPTION_EXTRA_LENGTH] = {0};
    QString path = Settings::getInstance().getSettingsDirPath() + name + ".tox";
    QFile saveFile(path);
    if (!saveFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Couldn't open tox save " << path;
        return false;
    }

    saveFile.read((char*)data, TOX_PASS_ENCRYPTION_EXTRA_LENGTH);
    saveFile.close();

    return tox_is_data_encrypted(data);
}

/**
 * @brief Removes the profile permanently.
 * Updates the profiles vector.
 * @return Vector of filenames that could not be removed.
 * @warning It is invalid to call loadToxSave or saveToxSave on a deleted profile.
 */
QStringList Profile::remove()
{
    if (isRemoved) {
        qWarning() << "Profile " << name << " is already removed!";
        return {};
    }
    isRemoved = true;

    qDebug() << "Removing profile" << name;
    for (int i = 0; i < profiles.size(); ++i) {
        if (profiles[i] == name) {
            profiles.removeAt(i);
            i--;
        }
    }
    QString path = Settings::getInstance().getSettingsDirPath() + name;
    ProfileLocker::unlock();

    QFile profileMain{path + ".tox"};
    QFile profileConfig{path + ".ini"};

    QStringList ret;

    if (!profileMain.remove() && profileMain.exists()) {
        ret.push_back(profileMain.fileName());
        qWarning() << "Could not remove file " << profileMain.fileName();
    }
    if (!profileConfig.remove() && profileConfig.exists()) {
        ret.push_back(profileConfig.fileName());
        qWarning() << "Could not remove file " << profileConfig.fileName();
    }

    QString dbPath = getDbPath(name);
    if (database && database->isOpen() && !database->remove() && QFile::exists(dbPath)) {
        ret.push_back(dbPath);
        qWarning() << "Could not remove file " << dbPath;
    }

    history.reset();
    database.reset();

    return ret;
}

/**
 * @brief Tries to rename the profile.
 * @param newName New name for the profile.
 * @return False on error, true otherwise.
 */
bool Profile::rename(QString newName)
{
    QString path = Settings::getInstance().getSettingsDirPath() + name,
            newPath = Settings::getInstance().getSettingsDirPath() + newName;

    if (!ProfileLocker::lock(newName)) {
        return false;
    }

    QFile::rename(path + ".tox", newPath + ".tox");
    QFile::rename(path + ".ini", newPath + ".ini");
    if (database) {
        database->rename(newName);
    }

    bool resetAutorun = Settings::getInstance().getAutorun();
    Settings::getInstance().setAutorun(false);
    Settings::getInstance().setCurrentProfile(newName);
    if (resetAutorun) {
        Settings::getInstance().setAutorun(true); // fixes -p flag in autostart command line
    }

    name = newName;
    return true;
}

const ToxEncrypt* Profile::getPasskey() const
{
    return passkey.get();
}

/**
 * @brief Delete core and restart a new one
 */
void Profile::restartCore()
{
    GUI::setEnabled(false); // Core::reset re-enables it

    if (core && !isRemoved) {
        // TODO(sudden6): there's a potential race condition between unlocking the core loop
        // and killing the core
        const QByteArray& savedata = core->getToxSaveData();

        // save to disk just in case
        if (saveToxSave(savedata)) {
            qDebug() << "Restarting Core";
            const bool isNewProfile{false};
            IAudioControl* audioBak = core->getAv()->getAudio();
            assert(audioBak != nullptr);
            initCore(savedata, Settings::getInstance(), isNewProfile);
            core->getAv()->setAudio(*audioBak);
            core->start();
        } else {
            qCritical() << "Failed to save, not restarting core";
        }
    }

    GUI::setEnabled(true);
}

/**
 * @brief Changes the encryption password and re-saves everything with it
 * @param newPassword Password for encryption, if empty profile will be decrypted.
 * @param oldPassword Supply previous password if already encrypted or empty QString if not yet
 * encrypted.
 * @return Empty QString on success or error message on failure.
 */
QString Profile::setPassword(const QString& newPassword)
{
    if (newPassword.isEmpty()) {
        // remove password
        encrypted = false;
    } else {
        std::unique_ptr<ToxEncrypt> newpasskey = ToxEncrypt::makeToxEncrypt(newPassword);
        if (!newpasskey) {
            qCritical()
                << "Failed to derive key from password, the profile won't use the new password";
            return tr(
                "Failed to derive key from password, the profile won't use the new password.");
        }
        // apply change
        passkey = std::move(newpasskey);
        encrypted = true;
    }

    // apply new encryption
    onSaveToxSave();

    bool dbSuccess = false;

    // TODO: ensure the database and the tox save file use the same password
    if (database) {
        dbSuccess = database->setPassword(newPassword);
    }

    QString error{};
    if (!dbSuccess) {
        error = tr("Couldn't change password on the database, it might be corrupted or use the old "
                   "password.");
    }

    QByteArray avatar = loadAvatarData(core->getSelfId().getPublicKey());
    saveAvatar(core->getSelfId().getPublicKey(), avatar);

    QVector<uint32_t> friendList = core->getFriendList();
    QVectorIterator<uint32_t> i(friendList);
    while (i.hasNext()) {
        const ToxPk friendPublicKey = core->getFriendPublicKey(i.next());
        saveAvatar(friendPublicKey, loadAvatarData(friendPublicKey));
    }
    return error;
}

/**
 * @brief Retrieves the path to the database file for a given profile.
 * @param profileName Profile name.
 * @return Path to database.
 */
QString Profile::getDbPath(const QString& profileName)
{
    return Settings::getInstance().getSettingsDirPath() + profileName + ".db";
}
