#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <string>
#include <cstring>
#include <vector>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>


namespace serial {

/**
 * @brief serial info class
 */
struct SerialInfo {
    std::string port_name;
    std::string port_path;
    unsigned short product_id{};
    unsigned short vendor_id{};
    std::string product;
    std::string manufacturer;
    std::string serial_number;

    /**
     * @brief List serial device info
     * @return Serial device info
     */
    static std::vector<SerialInfo> list_port() {
        std::vector<SerialInfo> serial_info_list;
        for (const auto& device_name : glob_device()) {
            SerialInfo info;
            info.port_name = device_name;
            info.port_path = "/dev/" + device_name;
            std::string sys_device_path = get_sys_device_path(device_name);
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
        os  << s.port_path << ", "
            << std::setfill('0') << std::setw(4) << std::right << std::hex << s.product_id << ":"
            << std::setfill('0') << std::setw(4) << std::right << std::hex << s.vendor_id << ", "
            << s.manufacturer << ", "
            << s.product << ", "
            << s.serial_number;
        return os;
    }

private:
    static std::vector<std::string> glob_device() {
        std::vector<std::string> device_names;
        for (const auto& dir_entry : std::filesystem::directory_iterator{"/dev"}) {
            if (dir_entry.path().filename().string().find("ttyACM") == 0)
                device_names.emplace_back(dir_entry.path().filename());
            if (dir_entry.path().filename().string().find("ttyS") == 0)
                device_names.emplace_back(dir_entry.path().filename());
            if (dir_entry.path().filename().string().find("ttyUSB") == 0)
                device_names.emplace_back(dir_entry.path().filename());
            if (dir_entry.path().filename().string().find("tty.") == 0)
                device_names.emplace_back(dir_entry.path().filename());
            if (dir_entry.path().filename().string().find("cu.") == 0)
                device_names.emplace_back(dir_entry.path().filename());
            if (dir_entry.path().filename().string().find("rfcomm") == 0)
                device_names.emplace_back(dir_entry.path().filename());
        }
        return device_names;
    }

    static std::string get_sys_device_path(const std::string& device_name) {
        std::filesystem::path device_path = "/sys/class/tty";
        device_path = std::filesystem::canonical(device_path / device_name / "device");
        if (device_name.compare(0, 6, "ttyUSB") == 0) {
            device_path = device_path.parent_path().parent_path();
        } else if (device_name.compare(0, 6, "ttyACM") == 0) {
            device_path = device_path.parent_path();
        } else {
            return {};
        }
        if (std::filesystem::exists(device_path)) {
            return device_path;
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
    Serial(const std::string& port_name, const int baudrate) : port_name_(port_name),
                                                  baudrate_(baudrate),
                                                  data_bits_(8),
                                                  parity_bits_('N'),
                                                  stop_bits_(1) {}

    /**
   * @brief Destructor of serial device to close the device
   */
    ~Serial() {
        close_device();
    }

    /**
   * @brief Initialization of serial device to config and open the device
   * @return True if success
   */
    bool init() {
        if (port_name_.c_str() == nullptr)
            return false;
        if (open_device() && config_device()) {
            FD_ZERO(&serial_fd_set_);
            FD_SET(serial_fd_, &serial_fd_set_);
            return true;
        } else {
            close_device();
            return false;
        }
    }

    /**
   * @brief Serial device read function
   * @param buf Given buffer to be updated by reading
   * @param len Read data length
   * @return -1 if failed, else the read length
   */
    size_t read(uint8_t *buf, size_t len) {
        size_t ret = -1;
        if (nullptr == buf) {
            return -1;
        } else {
            ret = ::read(serial_fd_, buf, len);
            while (ret == 0) {
                while (!init()) {
                    usleep(500000);
                }
                ret = ::read(serial_fd_, buf, len);
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
    size_t write(const uint8_t *buf, size_t len) const {
        return ::write(serial_fd_, buf, len);
    }

private:
    /**
   * @brief Open the serial device
   * @return True if open successfully
   */
    bool open_device() {
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
    bool close_device() {
        close(serial_fd_);
        serial_fd_ = -1;
        return true;
    }

    /**
   * @brief Configure the device
   * @return True if configure successfully
   */
    bool config_device() {
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
