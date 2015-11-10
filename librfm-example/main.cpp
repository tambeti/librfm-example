//
//  main.cpp
//  librfm-example
//
//  Created by Tambet Ingo on 06/11/15.
//  Copyright © 2015 litl. All rights reserved.
//

#include <memory>
#include <mutex>
#include <tuple>

#include <log.h>
#include <media_util.h>
#include <model.h>
#include <restclient/service.h>

#include "media-reader.hpp"

#define BASE_URL "https://apiary-dev.roomformore.com"
#define MODEL_PATH "model-db"
#define EMAIL "tingo+example@litl.com"
#define PASSWORD "woven123!"

static std::unique_ptr<rfm::User> CreateUser(rfm::restclient::Service* service,
                                             const char* email,
                                             const char* password) {
    rfm::User user;
    user.set_email(email);
    
    rfm::restclient::UserResponse response;
    rfm::restclient::Status status;
    std::tie(response, status) = service->CreateUser(user, password, nullptr);
    if (status.ok() && response.status() == 200) {
        return std::make_unique<rfm::User>(response.user());
    } else if (status.ok()) {
        rfm::LOGE("User creation failed: %d: %s - %s",
                  response.status(),
                  response.error().c_str(),
                  response.error_desc().c_str());
    } else {
        rfm::LOGE("User creation failed: %s", status.msg());
    }
    
    return nullptr;
}

static std::tuple<rfm::User, std::string> GetToken(rfm::restclient::Service* service,
                                                   const char* email,
                                                   const char* password) {
    rfm::restclient::TokenResponse response;
    rfm::restclient::Status status;
    std::tie(response, status) = service->GetToken(email, password);
    if (status.ok() && response.status() == 200) {
        return std::make_tuple(response.user(), response.access_token());
    } else if (status.ok()) {
        rfm::LOGE("Fetching token failed: %d: %s - %s",
                  response.status(),
                  response.error().c_str(),
                  response.error_desc().c_str());
    } else {
        rfm::LOGE("Fetching token failed: %s", status.msg());
    }
    
    return std::make_tuple(rfm::User(), std::string());
}

static bool DeleteUser(rfm::restclient::Service* service,
                       const char* password) {
    rfm::restclient::JsonResponse response;
    rfm::restclient::Status status;
    std::tie(response, status) = service->DeleteUser(password);
    if (status.ok() && response.status() == 200) {
        return true;
    } else if (status.ok()) {
        rfm::LOGE("Deleting user failed: %d: %s - %s",
                  response.status(),
                  response.error().c_str(),
                  response.error_desc().c_str());
    } else {
        rfm::LOGE("Deleting user failed: %s", status.msg());
    }
    
    return false;
}

static bool SignOut(rfm::restclient::Service* service) {
    rfm::restclient::JsonResponse response;
    rfm::restclient::Status status;
    std::tie(response, status) = service->Logout();
    if (status.ok() && response.status() == 200) {
        return true;
    } else if (status.ok()) {
        rfm::LOGE("Signing out failed: %d: %s - %s",
                  response.status(),
                  response.error().c_str(),
                  response.error_desc().c_str());
    } else {
        rfm::LOGE("Deleting user failed: %s", status.msg());
    }
    
    return false;
}

static rfm::Source CreateLocalSource() {
    rfm::Source source;
    source.set_type("android");
    source.set_handle("123456");
    source.set_title("Android");
    source.set_subtitle("Nexus 7");
    source.set_category("tablet");
    
    return source;
}

static void MySyncListener(rfm::SyncStats stats) {
    if (stats.config_changed) {
        // App should read new config using Model.ReadConfig()
        rfm::LOGD("Model config changed");
    }
    if (stats.user_changed) {
        // App should read new user using Model.ReadUser();
        rfm::LOGD("Model user changed");
    }
    if (stats.sources_changed) {
        // App should read new sources using Model.NewSourceIterator();
        rfm::LOGD("Model sources changed");
    }

    // If any of the following is true, app should read new Media iterator(s)
    // using:
    // Model.NewMediaIterator() - medias ordered by handle
    // Model.NewAllMediaIterator() - medias ordered by creation time in descending order
    // Model.NewSourceMediaIterator() - medias filtered by source
    // Model.NewMediaByStateIterator() - medias filtered by state (local/remote)

    // If you need random access, wrap Media iterator in a MediaList.
    
    if (stats.medias_changed > 0) {
        rfm::LOGD("Model medias changed: %d", stats.medias_changed);
    }
    if (stats.medias_inserted > 0) {
        rfm::LOGD("Model medias inserted: %d", stats.medias_inserted);
    }
    if (stats.medias_removed > 0) {
        rfm::LOGD("Model medias removed: %d", stats.medias_removed);
    }
}

static void OpenModel(rfm::Model* model,
                      const std::string& path,
                      const rfm::Source& local_source) {
    std::mutex mutex;
    std::condition_variable cond;
    
    auto cb = [&](leveldb::Status s) {
        std::unique_lock<std::mutex> lck(mutex);
        cond.notify_all();
    };
    
    // All Model modification API is asyncronous and takes callbacks
    // which get invoked when the operations are done. Use a condition
    // variable here which gets notified when the model has been opened
    // to turn it to synchronous.
    model->Start(path, local_source, cb);
    std::unique_lock<std::mutex> lck(mutex);
    cond.wait(lck);
}

static bool AddLocalMedias(rfm::Model* model,
                           const rfm::Source& source,
                           const char* directory) {
    auto medias = ReadMediaDirectory(source, directory);
    rfm::LOGD("Found medias: %d", medias.size());
    if (medias.empty()) {
        return false;
    }

    std::mutex mutex;
    std::condition_variable cond;

    auto cb = [&](leveldb::Status s) {
        std::unique_lock<std::mutex> lck(mutex);
        cond.notify_all();
    };
    
    // Again, wait for the operation to finish
    model->MergeLocalMedias(medias, cb);
    std::unique_lock<std::mutex> lck(mutex);
    cond.wait(lck);
    
    return true;
}

static bool IsLocalOnlyMedia(const rfm::Media& media) {
    for (auto& size : media.sizes()) {
        if (!IsLocalSize(size)) {
            return false;
        }
    }
    
    return true;
}

static std::vector<rfm::Handle> LocalHandles(rfm::Model* model) {
    std::vector<rfm::Handle> local_handles;
    auto media_iter = model->NewMediaByStateIterator(rfm::MediaState::kLocal);
    rfm::Media media;
    media_iter->SeekToFirst();
    while (media_iter->Valid()) {
        leveldb::Status s = media_iter->Get(&media);
        if (s.ok()) {
            if (IsLocalOnlyMedia(media)) {
                local_handles.push_back(media.handle());
            }
        } else {
            rfm::LOGE("Could not read media: %s", s.ToString().c_str());
        }
        
        media_iter->Next();
    }

    return local_handles;
}

// Upload all local Media to the server.
// Also see Model.NextBatch() and Model.ReviewBatch()
// to get a selection of Medias for RFM batch screen.
static void UploadMedias(rfm::Model* model) {
    auto local_handles = LocalHandles(model);
    if (local_handles.empty()) {
        rfm::LOGW("No local medias found");
        return;
    }
    
    auto uploader = model->uploader();
    std::mutex mutex;
    std::condition_variable cond;

    auto token = uploader->AddSessionListener([&](rfm::uploader::Session session) {
        rfm::LOGD("Session changed, %d/%d ", session.completed, session.total);
        if (session.completed == local_handles.size()) {
            std::unique_lock<std::mutex> lck(mutex);
            cond.notify_all();
        }
    });
  
    uploader->Add(local_handles);
    
    std::unique_lock<std::mutex> lck(mutex);
    cond.wait(lck);
    uploader->RemoveSessionListener(token);
}

static rfm::SyncStats Sync(rfm::Model* model) {
    rfm::SyncStats stats;
    std::mutex mutex;
    std::condition_variable cond;
    
    auto cb = [&](const rfm::SyncStats& s,
                  leveldb::Status status) {
        if (status.ok()) {
            stats = s;
        }
        
        std::unique_lock<std::mutex> lck(mutex);
        cond.notify_all();
    };
    
    model->Sync(false, false, cb);
    std::unique_lock<std::mutex> lck(mutex);
    cond.wait(lck);
    
    return stats;
}

static std::string MediaToString(const rfm::Media& media) {
    std::string str = "Media ";
    auto handle = media.handle();
    str.append(handle.source_type())
        .append("/")
        .append(handle.source_handle())
        .append("/")
        .append(handle.handle())
        .append("\n");
    
    for (auto& size : media.sizes()) {
        str.append("\t")
            .append(size.url())
            .append("\n");
    }
    
    return str;
}

static void PrintMedias(rfm::Model* model) {
    rfm::Media media;
    auto media_iter = model->NewAllMediaIterator();
    media_iter->SeekToFirst();
    while (media_iter->Valid()) {
        leveldb::Status s = media_iter->Get(&media);
        if (s.ok()) {
            std::string str = MediaToString(media);
            rfm::LOGI("%s", str.c_str());
        } else {
            rfm::LOGE("Could not read media: %s", s.ToString().c_str());
        }
        
        media_iter->Next();
    }
}


// This example shows how to:
// 1. Create a user on the server
// 2. Add local Media to the Model
// 3. Upload local Media to server
// 4. Sign out
static bool NewUserExample(const char* media_dir) {
    // First thing needed to create a Model instance is a REST client Service.
    // This is for calling REST API on the Apiary server.
    auto service = std::make_shared<rfm::restclient::Service>(BASE_URL);
    
    // Next, Model needs a User, so lets create one on the server
    auto user = CreateUser(service.get(), EMAIL, PASSWORD);
    if (!user) {
        return false;
    }
    
    // The service needs to be authenticated, fetch a token for it
    rfm::User user_from_server;
    std::string token;
    std::tie(user_from_server, token) = GetToken(service.get(), EMAIL, PASSWORD);
    if (!token.empty()) {
        service->SetToken(token.c_str());
    } else {
        return false;
    }
    
    // The final thing needed for Model is the local Source
    auto local_source = CreateLocalSource();
    
    // Finally, we can create a Model
    auto model = std::make_shared<rfm::Model>(service);
    OpenModel(model.get(), MODEL_PATH, local_source);
    model->PutUser(*(user.get()));
    
    // Register a "sync" listener to the Model. It gets called
    // every time the Model changes and reports what exactly changed.
    model->SyncStatsListener(MySyncListener);
    
    // Add local medias to Model
    bool found_medias = AddLocalMedias(model.get(), local_source, media_dir);
    if (found_medias) {
        // Upload all local medias
        UploadMedias(model.get());
    
        // Print out all Medias from the database with sizes
        PrintMedias(model.get());
    }
    
    // Finally, stop the model, sign out, delete the local database
    model->Stop();
    model.reset();
    SignOut(service.get());
    
    leveldb::Status s = leveldb::DestroyDB(MODEL_PATH, leveldb::Options());
    if (!s.ok()) {
        rfm::LOGW("Could not destroy db: %s", s.ToString().c_str());
    }
    
    return true;
}

// This example shows how to:
// 1. Log in with an existing user
// 2. Sync with the server to get remote Media
// 3. Merge in local Media
// 4. Delete the user
static bool ExistingUserExample(const char* media_dir) {
    // First thing needed to create a Model instance is a REST client Service.
    // This is for calling REST API on the Apiary server.
    auto service = std::make_shared<rfm::restclient::Service>(BASE_URL);
    
    // Next, sign in
    rfm::User user;
    std::string token;
    std::tie(user, token) = GetToken(service.get(), EMAIL, PASSWORD);
    if (!token.empty()) {
        service->SetToken(token.c_str());
    } else {
        return false;
    }
    
    // The final thing needed for Model is the local Source
    auto local_source = CreateLocalSource();
    
    // Finally, we can create a Model
    auto model = std::make_shared<rfm::Model>(service);
    OpenModel(model.get(), MODEL_PATH, local_source);
    model->PutUser(user);
    
    // Register a "sync" listener to the Model. It gets called
    // every time the Model changes and reports what exactly changed.
    model->SyncStatsListener(MySyncListener);
    
    // Sync with the server to get information about user Media
    Sync(model.get());
 
    // Add local medias to Model. If a Media already exists in the
    // Model (synced from server), its local size is merged with the
    // existing Media.
    AddLocalMedias(model.get(), local_source, media_dir);

    // Print out all Medias from the database with sizes
    PrintMedias(model.get());

    // That's it, let's clean everyting up
    
    // First, stop the model
    model->Stop();
    model.reset();
    // Then delete the user from the server
    DeleteUser(service.get(), PASSWORD);
    // And delete the local database
    leveldb::Status s = leveldb::DestroyDB(MODEL_PATH, leveldb::Options());
    if (!s.ok()) {
        rfm::LOGW("Could not destroy db: %s", s.ToString().c_str());
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        rfm::LOGI("Usage: %s <local_media_directroy>", argv[0]);
        return 1;
    }
    
    const char* media_dir = argv[1];

    // Let's start from a clean state with a new user
    NewUserExample(media_dir);

    // Next, here's how to sign in an existing user
    ExistingUserExample(media_dir);
    
    return 0;
}
