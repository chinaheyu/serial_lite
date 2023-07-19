#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <sys/stat.h>
#include <iomanip>

namespace serial {

/**
 * @brief serial info class
 */
struct SerialInfo {
    std::string port_name;
    unsigned short product_id{};
    unsigned short vendor_id{};
    std::string product;
    std::string manufacturer;
    std::string serial_number;

    /**
     * @brief List serial device info
     * @return Serial device info
     */
    static std::vector<SerialInfo> ListPort() {
        std::vector<SerialInfo> serial_info_list;
        std::vector<std::string> device_names(glob_device());
        for (std::vector<std::string>::const_iterator it = device_names.begin(); it != device_names.end(); ++it) {
            SerialInfo info;
            info.port_name = *it;
            std::string sys_device_path = get_sys_device_path(*it);
            if (!sys_device_path.empty()) {
                info.product_id = stoul(read_line(sys_device_path + "/idProduct"), nullptr, 16);
                info.vendor_id = stoul(read_line(sys_device_path + "/idVendor"), nullptr, 16);
                info.product = read_line(sys_device_path + "/product");
                info.manufacturer = read_line(sys_device_path + "/manufacturer");
                info.serial_number = read_line(sys_device_path + "/serial");
            }
            serial_info_list.emplace_back(info);
        }
        return serial_info_list;
    }

    friend std::ostream& operator<<(std::ostream& os, const SerialInfo& s) {
        os  << s.port_name << ", "
            << std::setfill('0') << std::setw(4) << std::right << std::hex << s.product_id << ":"
            << std::setfill('0') << std::setw(4) << std::right << std::hex << s.vendor_id << ", "
            << s.manufacturer << ", "
            << s.product << ", "
            << s.serial_number;
    }

private:
    static std::vector<std::string> glob_device() {
        std::vector<std::string> device_names;
        std::vector<std::string> patterns {
                "/dev/ttyACM*",
                "/dev/ttyS*",
                "/dev/ttyUSB*",
                "/dev/tty.*",
                "/dev/cu.*",
                "/dev/rfcomm*"
        };
        glob_t glob_results;
        int glob_retval = glob(patterns[0].c_str(), 0, nullptr, &glob_results);
        std::vector<std::string>::const_iterator iter = patterns.begin();
        while(++iter != patterns.end())
            glob_retval = glob(iter->c_str(), GLOB_APPEND, nullptr, &glob_results);
        for(int path_index = 0; path_index < glob_results.gl_pathc; path_index++) {
            std::string path_found(glob_results.gl_pathv[path_index]);
            device_names.emplace_back(path_found.substr(path_found.rfind('/') + 1, std::string::npos));
        }
        globfree(&glob_results);
        return device_names;
    }

    static std::string get_sys_device_path(const std::string& device_name) {
        char path_template[] = "/sys/class/tty/%s/device";
        size_t len = snprintf(nullptr, 0, path_template, device_name.c_str());
        char* device_path = new char[len + 1];
        std::snprintf(device_path, len + 1, path_template, device_name.c_str());
        char* real_device_path_buf = realpath(device_path, nullptr);
        delete[] device_path;
        std::string real_device_path;
        if (real_device_path_buf != nullptr) {
            real_device_path = real_device_path_buf;
            free(real_device_path_buf);
        }
        std::string sys_device_path;
        size_t pos;
        if (device_name.compare(0,6,"ttyUSB") == 0) {
            pos = real_device_path.rfind('/');
            pos = real_device_path.rfind('/', pos - 1);
            sys_device_path = real_device_path.substr(0, pos);
        } else if (device_name.compare(0,6,"ttyACM") == 0) {
            pos = real_device_path.rfind('/');
            sys_device_path = real_device_path.substr(0, pos);
        }
        if (path_exists(sys_device_path)) {
            return sys_device_path;
        }
        return {};
    }

    static std::string read_line(const std::string& file) {
        std::ifstream ifs(file.c_str(), std::ifstream::in);
        std::string line;
        if(ifs)
            getline(ifs, line);
        return line;
    }

    static bool path_exists(const std::string& path) {
        struct stat sb;
        if (path.empty())
            return false;
        if(stat(path.c_str(), &sb) == 0 )
            return true;
        return false;
    }
};

/**
 * @brief serial class
 */
class Serial {
public:
    /**
   * @brief Constructor of serial device
   * @param port_name port name, i.e. /dev/ttyUSB0
   * @param baudrate serial baudrate
   */
    Serial(const std::string port_name, const int baudrate) : port_name_(port_name),
                                                  baudrate_(baudrate),
                                                  data_bits_(8),
                                                  parity_bits_('N'),
                                                  stop_bits_(1) {}

    /**
   * @brief Destructor of serial device to close the device
   */
    ~Serial() {
        CloseDevice();
    }

    /**
   * @brief Initialization of serial device to config and open the device
   * @return True if success
   */
    bool Init() {
        if (port_name_.c_str() == nullptr)
            return false;
        if (OpenDevice() && ConfigDevice()) {
            FD_ZERO(&serial_fd_set_);
            FD_SET(serial_fd_, &serial_fd_set_);
            return true;
        } else {
            CloseDevice();
            return false;
        }
    }

    /**
   * @brief Serial device read function
   * @param buf Given buffer to be updated by reading
   * @param len Read data length
   * @return -1 if failed, else the read length
   */
    size_t Read(uint8_t *buf, size_t len) {
        size_t ret = -1;
        if (nullptr == buf) {
            return -1;
        } else {
            ret = read(serial_fd_, buf, len);
            while (ret == 0) {
                while (!Init()) {
                    usleep(500000);
                }
                ret = read(serial_fd_, buf, len);
            }
            return ret;
        }
    }

    /**
   * @brief Write the buffer data into device to send the data
   * @param buf Given buffer to be sent
   * @param len Send data length
   * @return < 0 if failed, else the send length
   */
    size_t Write(const uint8_t *buf, size_t len) const {
        return write(serial_fd_, buf, len);
    }

private:
    /**
   * @brief Open the serial device
   * @return True if open successfully
   */
    bool OpenDevice() {
#ifdef __arm__
        serial_fd_ = open(port_name_.c_str(), O_RDWR | O_NONBLOCK);
#elif __x86_64__
        serial_fd_ = open(port_name_.c_str(), O_RDWR | O_NOCTTY);
#else
        serial_fd_ = open(port_name_.c_str(), O_RDWR | O_NOCTTY);
#endif
        if (serial_fd_ < 0)
            return false;
        return true;
    }

    /**
   * @brief Close the serial device
   * @return True if close successfully
   */
    bool CloseDevice() {
        close(serial_fd_);
        serial_fd_ = -1;
        return true;
    }

    /**
   * @brief Configure the device
   * @return True if configure successfully
   */
    bool ConfigDevice() {
        int st_baud[] = {B4800, B9600, B19200, B38400,
                         B57600, B115200, B230400, B921600};
        int std_rate[] = {4800, 9600, 19200, 38400, 57600, 115200,
                          230400, 921600, 1000000, 1152000, 3000000};
        int i, j;
        /* save current port parameter */
        if (tcgetattr(serial_fd_, &old_termios_) != 0) {
            return false;
        }
        memset(&new_termios_, 0, sizeof(new_termios_));
        /* config the size of char */
        new_termios_.c_cflag |= CLOCAL | CREAD;
        new_termios_.c_cflag &= ~CSIZE;
        /* config data bit */
        switch (data_bits_) {
            case 7:
                new_termios_.c_cflag |= CS7;
                break;
            case 8:
                new_termios_.c_cflag |= CS8;
                break;
            default:
                new_termios_.c_cflag |= CS8;
                break; //8N1 default config
        }
        /* config the parity bit */
        switch (parity_bits_) {
            /* odd */
            case 'O':
            case 'o':
                new_termios_.c_cflag |= PARENB;
                new_termios_.c_cflag |= PARODD;
                break;
                /* even */
            case 'E':
            case 'e':
                new_termios_.c_cflag |= PARENB;
                new_termios_.c_cflag &= ~PARODD;
                break;
                /* none */
            case 'N':
            case 'n':
                new_termios_.c_cflag &= ~PARENB;
                break;
            default:
                new_termios_.c_cflag &= ~PARENB;
                break; //8N1 default config
        }
        /* config baudrate */
        j = sizeof(std_rate) / 4;
        for (i = 0; i < j; ++i) {
            if (std_rate[i] == baudrate_) {
                /* set standard baudrate */
                cfsetispeed(&new_termios_, st_baud[i]);
                cfsetospeed(&new_termios_, st_baud[i]);
                break;
            }
        }
        /* config stop bit */
        if (stop_bits_ == 1)
            new_termios_.c_cflag &= ~CSTOPB;
        else if (stop_bits_ == 2)
            new_termios_.c_cflag |= CSTOPB;
        else
            new_termios_.c_cflag &= ~CSTOPB; //8N1 default config
        /* config waiting time & min number of char */
        new_termios_.c_cc[VTIME] = 1;
        new_termios_.c_cc[VMIN] = 18;
        /* using the raw data mode */
        new_termios_.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        new_termios_.c_oflag &= ~OPOST;
        /* flush the hardware fifo */
        tcflush(serial_fd_, TCIFLUSH);
        /* activite the configuration */
        if ((tcsetattr(serial_fd_, TCSANOW, &new_termios_)) != 0)
            return false;
        return true;
    }

    //! port name of the serial device
    std::string port_name_;
    //! baudrate of the serial device
    int baudrate_;
    //! stop bits of the serial device, as default
    int stop_bits_;
    //! data bits of the serial device, as default
    int data_bits_;
    //! parity bits of the serial device, as default
    char parity_bits_;
    //! serial handler
    int serial_fd_;
    //! set flag of serial handler
    fd_set serial_fd_set_;
    //! termios config for serial handler
    struct termios new_termios_, old_termios_;
};

}

#endif //__SERIAL_H__
