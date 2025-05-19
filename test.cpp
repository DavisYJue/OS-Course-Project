#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>  

using namespace std;

static const int FS_SIZE = 16 * 1024 * 1024;
static const int BLOCK_SIZE = 1024;
static const int NUM_BLOCKS = FS_SIZE / BLOCK_SIZE;
static const int MAX_INODES = 1024;          
static const int DIRECT_PTRS = 10;

inline int addr_to_block(int addr) { return addr >> 10; }
inline int addr_to_offset(int addr) { return addr & 0x3FF; }
inline int make_addr(int blk, int off) { return (blk << 10) | off; }

struct Superblock {
    int total_blocks;
    int free_blocks;
    vector<bool> block_bitmap;
    Superblock()
        : total_blocks(NUM_BLOCKS), free_blocks(NUM_BLOCKS),
          block_bitmap(NUM_BLOCKS, true) {}
};

struct Inode {
    bool used;
    int size;
    time_t ctime;          
    int direct[DIRECT_PTRS];
    int indirect;          
    bool is_directory;
    Inode() : used(false), size(0), ctime(0), indirect(-1), is_directory(false) {
        memset(direct, -1, sizeof(direct));
    }
};

struct DirEntry {
    string name;
    int inode_idx;
};

class FileSystem {
private:
    Superblock sb;
    vector<Inode> inodes;
    vector<char*> data_blocks;
    map<string, vector<DirEntry>> directories; 
    string cwd;

public:
    FileSystem()
        : inodes(MAX_INODES), data_blocks(NUM_BLOCKS, nullptr), cwd("/") {
        for (int i = 0; i < NUM_BLOCKS; ++i)
            data_blocks[i] = new char[BLOCK_SIZE];
        init_root();
    }

    ~FileSystem() {
        for (auto ptr : data_blocks)
            delete[] ptr;
    }

    void init_root() {
        Inode &root = inodes[0];
        root.used = true;
        root.is_directory = true;
        root.ctime = time(nullptr);
        directories["/"] = {};
    }

    int alloc_inode() {
        for (int i = 0; i < MAX_INODES; ++i) {
            if (!inodes[i].used) {
                inodes[i].used = true;
                inodes[i].ctime = time(nullptr);
                return i;
            }
        }
        return -1;
    }

    int alloc_block() {
        for (int i = 0; i < NUM_BLOCKS; ++i) {
            if (sb.block_bitmap[i]) {
                sb.block_bitmap[i] = false;
                sb.free_blocks--;
                memset(data_blocks[i], 0, BLOCK_SIZE);
                return i;
            }
        }
        return -1;
    }

    void free_block(int idx) {
        if (idx < 0) return;
        sb.block_bitmap[idx] = true;
        sb.free_blocks++;
    }

    string abs_path(const string &path) {
        if (path.empty()) return cwd;
        if (path[0] == '/') return path;
        if (cwd == "/") return "/" + path;
        return cwd + "/" + path;
    }

    int lookup_inode(const string &path) {
        string ap = abs_path(path);
        string parent = ap.substr(0, ap.find_last_of('/'));
        if (parent.empty()) parent = "/";
        string name = ap.substr(ap.find_last_of('/') + 1);
        for (auto &e : directories[parent]) {
            if (e.name == name) return e.inode_idx;
        }
        return -1;
    }

    void cmd_createDir(const string &path) {
        string ap = abs_path(path);
        if (directories.count(ap)) {
            cout << "Directory already exists\n";
            return;
        }

        size_t lastSlash = ap.find_last_of('/');
        string parent = ap.substr(0, lastSlash);
        if (parent.empty()) parent = "/";
        if (!directories.count(parent)) {
            cout << "Error: Parent directory '" << parent << "' does not exist\n";
            return;
        }

        int ino = alloc_inode();

        if (ino < 0) { cout << "No free inode\n"; return; }
        Inode &din = inodes[ino];
        din.used = true;
        din.is_directory = true;
        din.ctime = time(nullptr);
        
        int block = alloc_block();
        
        if (block < 0) { 
            cout << "No space for directory block\n"; 
            inodes[ino] = Inode();
            return; 
        }
        din.direct[0] = block;

        directories[ap] = {};
        string name = ap.substr(lastSlash + 1);
        directories[parent].push_back({name, ino});
        cout << "Directory created: " << ap << "\n";
    }

    void cmd_deleteDir(const string &path) {
        string ap = abs_path(path);
        if (ap == cwd) {
            cout << "Cannot delete current directory\n";
            return;
        }
        auto it = directories.find(ap);
        if (it == directories.end()) { cout << "Directory not found\n"; return; }
        if (!it->second.empty()) { cout << "Directory not empty\n"; return; }
        int ino = lookup_inode(path);
        Inode &din = inodes[ino];
        free_block(din.direct[0]);
        inodes[ino] = Inode();
        directories.erase(it);
        string parent = ap.substr(0, ap.find_last_of('/'));
        if (parent.empty()) parent = "/";
        string name = ap.substr(ap.find_last_of('/') + 1);
        auto &pe = directories[parent];
        pe.erase(remove_if(pe.begin(), pe.end(), [&](const DirEntry &d) {
            return d.name == name;
        }), pe.end());
        cout << "Directory deleted: " << ap << "\n";
    }

    void cmd_changeDir(const string &path) {
        if (path == "..") {
        if (cwd == "/") {
            cout << "Already at root directory\n";
            return;
        }
    
        size_t last_slash = cwd.find_last_of('/');
        string parent = cwd.substr(0, last_slash);
        if (parent.empty()) parent = "/";
        if (!directories.count(parent)) {
            cout << "Parent directory not found\n";
            return;
        }

        cwd = parent;
        cout << "Current directory: " << cwd << "\n";
        } else {
        string ap = abs_path(path);
        if (!directories.count(ap)) { 
            cout << "Directory not found\n"; 
            return; 
        }

        cwd = ap;
        cout << "Current directory: " << cwd << "\n";
        }
    }

    void cmd_dir() {
        const auto& entries = directories[cwd];
        std::vector<DirEntry> dirs, files;
        for (const auto& e : entries) {
            Inode& ino = inodes[e.inode_idx];
            if (ino.is_directory) dirs.push_back(e);
            else                 files.push_back(e);
        }

        auto cmp_ctime = [&](const DirEntry& a, const DirEntry& b) {
            return inodes[a.inode_idx].ctime < inodes[b.inode_idx].ctime;
        };

        std::sort(dirs.begin(),  dirs.end(), cmp_ctime);
        std::sort(files.begin(), files.end(), cmp_ctime);

        auto print_entry = [&](const DirEntry& e) {
            Inode& ino = inodes[e.inode_idx];
            cout << (ino.is_directory ? "[DIR]  " : "[FILE] ")
                << std::setw(20) << std::left << e.name;
            if (ino.is_directory) {
                int cnt = directories[abs_path(e.name)].size();
                cout << " entries = " << cnt;
            } else {
                cout << " size = " << ino.size << "B";
            }
            cout << ", created = " << ctime(&ino.ctime);
        };

        for (const auto& d : dirs)   print_entry(d);
        for (const auto& f : files)  print_entry(f);
    }


    void cmd_createFile(const string &path, int size_kb) {
        int size_bytes = size_kb * 1024;
        int needed = (size_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (needed > DIRECT_PTRS + (BLOCK_SIZE / sizeof(int))) { 
            cout << "Exceeds max file size\n"; 
            return; 
        }
        string ap = abs_path(path);

        string parent = ap.substr(0, ap.find_last_of('/'));
        if (parent.empty()) parent = "/";
        string name = ap.substr(ap.find_last_of('/') + 1);

        for (const auto& entry : directories[parent]) {
            if (entry.name == name) {
                cout << "Error: A file or directory with the name '" << name << "' already exists\n";
                return;
            }
        }

        int ino_idx = alloc_inode();
        if (ino_idx < 0) { cout << "No free inode\n"; return; }
        Inode &fin = inodes[ino_idx]; 
        fin.used = true; 
        fin.size = size_bytes; 
        fin.ctime = time(nullptr);

        vector<int> blocks;
        for (int i = 0; i < min(needed, DIRECT_PTRS); ++i) {
            int b = alloc_block(); 
            if (b < 0) { 
                cout << "No space\n"; 
                for (int blk : blocks) free_block(blk);
                inodes[ino_idx] = Inode();
                return; 
            }
            fin.direct[i] = b;
            blocks.push_back(b);
        }

        if (needed > DIRECT_PTRS) {
            int ib = alloc_block();
            if (ib < 0) { 
                cout << "No space\n"; 
                for (int blk : blocks) free_block(blk);
                inodes[ino_idx] = Inode();
                return; 
            }

            fin.indirect = ib;
            int *ptrs = reinterpret_cast<int*>(data_blocks[ib]);
            for (int i = 0; i < needed - DIRECT_PTRS; ++i) {
                int b = alloc_block(); 
                if (b < 0) { 
                    cout << "No space\n"; 
                    free_block(ib);
                    for (int blk : blocks) free_block(blk);
                    inodes[ino_idx] = Inode();
                    return; 
                }
                ptrs[i] = b;
                blocks.push_back(b);
            }
        }

        srand(time(nullptr));
        int written = 0;
        for (int b : blocks) {
            for (int i = 0; i < BLOCK_SIZE && written < size_bytes; ++i, ++written)
                data_blocks[b][i] = 'A' + rand() % 26;
        }

        directories[parent].push_back({name, ino_idx});
        cout << "File created: " << ap << " " << size_kb << "KB\n";
    }

    void cmd_deleteFile(const string &path) {
        string ap = abs_path(path);
        int ino_idx = lookup_inode(path);
        if (ino_idx < 0) { cout << "File not found\n"; return; }

        Inode &fin = inodes[ino_idx];
        for (int i = 0; i < DIRECT_PTRS; ++i) 
            free_block(fin.direct[i]);

        if (fin.indirect >= 0) {
            int *ptrs = reinterpret_cast<int*>(data_blocks[fin.indirect]);
            for (int i = 0; i < BLOCK_SIZE / sizeof(int); ++i) {
                if (ptrs[i] <= 0) break;
                free_block(ptrs[i]);
            }
            free_block(fin.indirect);
        }

        inodes[ino_idx] = Inode();
        string parent = ap.substr(0, ap.find_last_of('/'));

        if (parent.empty()) parent = "/";
        string name = ap.substr(ap.find_last_of('/') + 1);
        auto &pe = directories[parent];
        pe.erase(remove_if(pe.begin(), pe.end(), [&](const DirEntry &d) { 
            return d.name == name; 
        }), pe.end());

        cout << "File deleted: " << ap << "\n";
    }

    void cmd_cp(const string &src, const string &dst) {
        int sidx = lookup_inode(src);
        if (sidx < 0) {
            cout << "Source not found\n";
            return;
        }

        Inode &sin = inodes[sidx];
        string abs_src = abs_path(src);
        string abs_dst = abs_path(dst);

        if (sin.is_directory) {
            if (abs_dst == abs_src || abs_dst.find(abs_src + "/") == 0) {
            cout << "Error: Cannot copy a directory into its subdirectory\n";
            return;
            }
            cmd_createDir(dst);

            for (auto &entry : directories[abs_src]) {
                string child_name = entry.name;
                string child_src = abs_src + "/" + child_name;
                string child_dst = abs_dst + "/" + child_name;
                cmd_cp(child_src, child_dst);
            }
            return;
        }

        int didx = alloc_inode();
        if (didx < 0) {
            cout << "No free inode\n";
            return;
        }

        Inode &din = inodes[didx];
        din.used = true;
        din.size = sin.size;
        din.ctime = time(nullptr);

        for (int i = 0; i < DIRECT_PTRS; ++i) {
            if (sin.direct[i] < 0) break;
            int nb = alloc_block();
            if (nb < 0) {
                cout << "No space during copy\n";
                inodes[didx] = Inode();
                return;
            }

            memcpy(data_blocks[nb], data_blocks[sin.direct[i]], BLOCK_SIZE);
            din.direct[i] = nb;
        }

        if (sin.indirect >= 0) {
            int nib = alloc_block();
            if (nib < 0) {
                for (int i = 0; i < DIRECT_PTRS; ++i)
                    if (din.direct[i] >= 0) free_block(din.direct[i]);
                inodes[didx] = Inode();
                cout << "No space during copy\n";
                return;
            }

            din.indirect = nib;
            int *sptrs = reinterpret_cast<int*>(data_blocks[sin.indirect]);
            int *dptrs = reinterpret_cast<int*>(data_blocks[nib]);

            for (int i = 0; i < BLOCK_SIZE / sizeof(int); ++i) {
                if (sptrs[i] <= 0) break;
                int nb = alloc_block();
                if (nb < 0) {
                    for (int j = 0; j < DIRECT_PTRS; ++j)
                        if (din.direct[j] >= 0) free_block(din.direct[j]);
                    free_block(nib);
                    inodes[didx] = Inode();
                    cout << "No space during copy\n";
                    return;
                }
                memcpy(data_blocks[nb], data_blocks[sptrs[i]], BLOCK_SIZE);
                dptrs[i] = nb;
            }
        }

        string parent = abs_dst.substr(0, abs_dst.find_last_of('/'));
        if (parent.empty()) parent = "/";
        string name = abs_dst.substr(abs_dst.find_last_of('/') + 1);

        for (auto &entry : directories[parent]) {
            if (entry.name == name) {
                cout << "Error: Target '" << name << "' already exists\n";
                for (int i = 0; i < DIRECT_PTRS && din.direct[i] >= 0; ++i)
                    free_block(din.direct[i]);
                if (din.indirect >= 0) {
                    int *ptrs = reinterpret_cast<int*>(data_blocks[din.indirect]);
                    for (int i = 0; i < BLOCK_SIZE / sizeof(int); ++i)
                        if (ptrs[i] > 0) free_block(ptrs[i]);
                    free_block(din.indirect);
                }

                inodes[didx] = Inode();
                return;
            }
        }

        directories[parent].push_back({name, didx});
        cout << "Copied " << src << " to " << dst << "\n";
    }

    void cmd_sum() {
        cout << "Total blocks: " << sb.total_blocks
             << " Used: " << (sb.total_blocks - sb.free_blocks)
             << " Free: " << sb.free_blocks << "\n";
    }

    void cmd_cat(const string &path) {
        int ino_idx = lookup_inode(path);
        if (ino_idx < 0) { cout << "File not found\n"; return; }
        Inode &fin = inodes[ino_idx];
        int read = 0;
        for (int i = 0; i < DIRECT_PTRS && read < fin.size; ++i) {
            int b = fin.direct[i]; 
            if (b < 0) break;
            int toRead = min(BLOCK_SIZE, fin.size - read);
            cout.write(data_blocks[b], toRead);
            read += toRead;
        }

        if (read < fin.size && fin.indirect >= 0) {
            int *ptrs = reinterpret_cast<int*>(data_blocks[fin.indirect]);
            for (int i = 0; i < BLOCK_SIZE / sizeof(int) && read < fin.size; ++i) {
                int b = ptrs[i]; 
                if (b <= 0) break;
                int toRead = min(BLOCK_SIZE, fin.size - read);
                cout.write(data_blocks[b], toRead);
                read += toRead;
            }
        }
        cout << "\n";
    }

    void save_image(const string &file = "fs.img") {
        ofstream out(file, ios::binary);
        out.write((char*)&sb.total_blocks, sizeof(sb.total_blocks));
        out.write((char*)&sb.free_blocks, sizeof(sb.free_blocks));

        for (size_t i = 0; i < sb.block_bitmap.size(); ++i) {
            bool b = sb.block_bitmap[i];
            out.write((char*)&b, sizeof(b));
        }
        for (auto &ino : inodes) out.write((char*)&ino, sizeof(ino));

        size_t dirCount = directories.size();
        out.write((char*)&dirCount, sizeof(dirCount));
        for (const auto& pair : directories) {
            const string& path = pair.first;
            const vector<DirEntry>& entries = pair.second;
            size_t pathLen = path.size();
            out.write((char*)&pathLen, sizeof(pathLen));
            out.write(path.c_str(), pathLen);
            size_t entryCount = entries.size();
            out.write((char*)&entryCount, sizeof(entryCount));
            for (const auto& e : entries) {
                size_t nameLen = e.name.size();
                out.write((char*)&nameLen, sizeof(nameLen));
                out.write(e.name.c_str(), nameLen);
                out.write((char*)&e.inode_idx, sizeof(e.inode_idx));
            }
        }

        for (int i = 0; i < NUM_BLOCKS; ++i) out.write(data_blocks[i], BLOCK_SIZE);
        out.close();
    }

    void load_image(const string &file = "fs.img") {
        ifstream in(file, ios::binary);
        if (!in) return;
        in.read((char*)&sb.total_blocks, sizeof(sb.total_blocks));
        in.read((char*)&sb.free_blocks, sizeof(sb.free_blocks));
        for (size_t i = 0; i < sb.block_bitmap.size(); ++i) {
            bool b;
            in.read((char*)&b, sizeof(b));
            sb.block_bitmap[i] = b;
        }

        for (auto &ino : inodes) in.read((char*)&ino, sizeof(ino));

        size_t dirCount;
        in.read((char*)&dirCount, sizeof(dirCount));
        directories.clear();
        for (size_t i = 0; i < dirCount; ++i) {
            size_t pathLen;
            in.read((char*)&pathLen, sizeof(pathLen));
            string path(pathLen, '\0');
            in.read(&path[0], pathLen);
            size_t entryCount;
            in.read((char*)&entryCount, sizeof(entryCount));
            vector<DirEntry> entries;
            for (size_t j = 0; j < entryCount; ++j) {
                size_t nameLen;
                in.read((char*)&nameLen, sizeof(nameLen));
                string name(nameLen, '\0');
                in.read(&name[0], nameLen);
                int inode_idx;
                in.read((char*)&inode_idx, sizeof(inode_idx));
                entries.push_back({name, inode_idx});
            }
            directories[path] = entries;
        }

        for (int i = 0; i < NUM_BLOCKS; ++i) in.read(data_blocks[i], BLOCK_SIZE);
        in.close();
    }

    void run() {
        cout << "\n----------------------------------------------------------------------------------------------------------------------------\n";
        cout << "\nWelcome to UnixFS Simulator! Group: Davis Y Jue (20229990180), Gilbert (202269990192), Rafael Reynard Ricardo (202269990184)\n";
        cout << "© DGR Project. All rights reserved.\n";
        cout << "\n----------------------------------------------------------------------------------------------------------------------------\n\n";
        load_image();
        string line;
        while (true) {
            cout << "UnixFS " << cwd << " > ";
            if (!getline(cin, line)) break;
            string cmd;
            stringstream ss(line);
            ss >> cmd;
            if (cmd == "exit") {
                save_image();
                cout << "See You Next Time !\n\n";
                break;
            } else if (cmd == "createDir") {
                string p; ss >> p; cmd_createDir(p);
            } else if (cmd == "deleteDir") {
                string p; ss >> p; cmd_deleteDir(p);
            } else if (cmd == "changeDir") {
                string p; ss >> p; cmd_changeDir(p);
            } else if (cmd == "dir") {
                cmd_dir();
            } else if (cmd == "createFile") {
                string p; int sz; ss >> p >> sz; cmd_createFile(p, sz);
            } else if (cmd == "deleteFile") {
                string p; ss >> p; cmd_deleteFile(p);
            } else if (cmd == "cp") {
                string s, d; ss >> s >> d; cmd_cp(s, d);
            } else if (cmd == "sum") {
                cmd_sum();
            } else if (cmd == "cat") {
                string p; ss >> p; cmd_cat(p);
            } else {
                cout << "Unknown command\n";
            }
            cout << "\n";
        }
        
    }
};

int main() {
    FileSystem fs;
    fs.run();
    return 0;
}