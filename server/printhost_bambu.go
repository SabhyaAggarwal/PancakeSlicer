package main

import (
	"archive/zip"
	"bufio"
	"context"
	"crypto/md5"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"
)

const bambuUser = "bblp"

func sendBambu(ctx context.Context, u *upload) (string, error) {
	p := u.printer
	if strings.ContainsAny(u.filename, "\r\n/") {
		return "", errors.New("the file name cannot be sent over FTP")
	}
	plate, err := packagePlate(u.file)
	if err != nil {
		return "", err
	}
	digest := md5.New()
	if _, err := io.Copy(digest, u.file); err != nil {
		return "", err
	}
	if _, err := u.file.Seek(0, io.SeekStart); err != nil {
		return "", err
	}
	tlsConfig := p.tlsConfig(p.Serial)
	tlsConfig.ClientSessionCache = tls.NewLRUClientSessionCache(4)
	if err := ftpsUpload(ctx, p.Host, tlsConfig, u); err != nil {
		return "", err
	}
	if !u.start {
		return "Uploaded " + u.filename + " to " + p.Name + "'s SD card.", nil
	}
	u.starting()
	return mqttProjectFile(ctx, p, u, plate, strings.ToUpper(hex.EncodeToString(digest.Sum(nil))))
}

var plateEntry = regexp.MustCompile(`^Metadata/plate_[0-9]+\.gcode$`)

func packagePlate(f *os.File) (string, error) {
	st, err := f.Stat()
	if err != nil {
		return "", err
	}
	z, err := zip.NewReader(f, st.Size())
	if err != nil {
		return "", errors.New("the export is not a valid .gcode.3mf package")
	}
	for _, entry := range z.File {
		if plateEntry.MatchString(entry.Name) {
			return entry.Name, nil
		}
	}
	return "", errors.New("the .gcode.3mf package holds no plate G-code")
}

func dialTLS(ctx context.Context, host string, port int, config *tls.Config) (*tls.Conn, error) {
	dialer := &tls.Dialer{NetDialer: &net.Dialer{Timeout: 5 * time.Second}, Config: config}
	dctx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	conn, err := dialer.DialContext(dctx, "tcp", net.JoinHostPort(host, strconv.Itoa(port)))
	if err != nil {
		if cause := context.Cause(ctx); cause != nil {
			return nil, cause
		}
		return nil, netError(err, "the printer")
	}
	context.AfterFunc(ctx, func() { conn.SetDeadline(time.Unix(1, 0)) })
	return conn.(*tls.Conn), nil
}

type ftpConn struct {
	conn *tls.Conn
	r    *bufio.Reader
}

func (c *ftpConn) reply(timeout time.Duration) (int, string, error) {
	c.conn.SetReadDeadline(time.Now().Add(timeout))
	line, err := c.r.ReadString('\n')
	if err != nil {
		return 0, "", err
	}
	if len(line) < 4 {
		return 0, "", errors.New("malformed FTP reply")
	}
	code, err := strconv.Atoi(line[:3])
	if err != nil {
		return 0, "", errors.New("malformed FTP reply")
	}
	text := strings.TrimSpace(line[4:])
	if line[3] == '-' {
		for {
			next, err := c.r.ReadString('\n')
			if err != nil {
				return 0, "", err
			}
			if strings.HasPrefix(next, line[:3]+" ") {
				text = strings.TrimSpace(next[4:])
				break
			}
		}
	}
	return code, text, nil
}

func (c *ftpConn) cmd(want int, timeout time.Duration, format string, args ...any) (string, error) {
	c.conn.SetWriteDeadline(time.Now().Add(timeout))
	if _, err := fmt.Fprintf(c.conn, format+"\r\n", args...); err != nil {
		return "", err
	}
	code, text, err := c.reply(timeout)
	if err != nil {
		return "", err
	}
	if code/100 != want/100 && code != want {
		return text, &ftpError{code, text}
	}
	return text, nil
}

type ftpError struct {
	code int
	text string
}

func (e *ftpError) Error() string { return fmt.Sprintf("FTP %d %s", e.code, e.text) }

var pasvReply = regexp.MustCompile(`(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)`)

func ftpsUpload(ctx context.Context, host string, config *tls.Config, u *upload) (err error) {
	conn, err := dialTLS(ctx, host, 990, config)
	if err != nil {
		return err
	}
	defer conn.Close()
	c := &ftpConn{conn: conn, r: bufio.NewReader(conn)}
	step := "greeting"
	defer func() {
		var ftpErr *ftpError
		switch {
		case err == nil:
		case context.Cause(ctx) != nil:
			err = context.Cause(ctx)
		case errors.As(err, &ftpErr) && ftpErr.code == 530:
			err = errors.New("the printer rejected the access code; check it on the printer's screen (Settings > LAN only) and in the server's credential file")
		case errors.As(err, &ftpErr) && ftpErr.code == 522:
			err = errors.New("the printer refused the FTP data connection (TLS session reuse); report this with the printer's firmware version")
		case errors.As(err, &ftpErr):
			err = fmt.Errorf("the printer's file transfer failed at %s: %v", step, ftpErr)
		default:
			err = netError(err, "the printer (FTPS port 990, "+step+")")
		}
	}()
	if code, text, rerr := c.reply(15 * time.Second); rerr != nil {
		return rerr
	} else if code != 220 {
		return &ftpError{code, text}
	}
	step = "login"
	if _, err = c.cmd(331, 15*time.Second, "USER %s", bambuUser); err != nil {
		return err
	}
	if _, err = c.cmd(230, 15*time.Second, "PASS %s", u.secret); err != nil {
		return err
	}
	step = "setup"
	for _, command := range []string{"PBSZ 0", "PROT P", "TYPE I"} {
		if _, err = c.cmd(200, 15*time.Second, "%s", command); err != nil {
			return err
		}
	}
	text, err := c.cmd(227, 15*time.Second, "PASV")
	if err != nil {
		return err
	}
	m := pasvReply.FindStringSubmatch(text)
	if m == nil {
		return errors.New("malformed PASV reply")
	}
	hi, _ := strconv.Atoi(m[5])
	lo, _ := strconv.Atoi(m[6])
	step = "data connection"
	raw, err := (&net.Dialer{Timeout: 5 * time.Second}).DialContext(ctx, "tcp", net.JoinHostPort(host, strconv.Itoa(hi<<8|lo)))
	if err != nil {
		return err
	}
	defer raw.Close()
	step = "upload"
	if _, err = c.cmd(150, 15*time.Second, "STOR %s", u.filename); err != nil {
		return err
	}
	data := tls.Client(raw, config)
	stop := context.AfterFunc(ctx, func() { raw.SetDeadline(time.Unix(1, 0)) })
	defer stop()
	data.SetDeadline(time.Now().Add(15 * time.Second))
	if err = data.HandshakeContext(ctx); err != nil {
		return err
	}
	data.SetDeadline(time.Time{})
	buf := make([]byte, 64<<10)
	for {
		n, rerr := u.file.Read(buf)
		if n > 0 {
			data.SetWriteDeadline(time.Now().Add(sendStall))
			if _, err = data.Write(buf[:n]); err != nil {
				return err
			}
			u.progress.Add(int64(n))
		}
		if rerr == io.EOF {
			break
		}
		if rerr != nil {
			return rerr
		}
	}
	if err = data.Close(); err != nil {
		return err
	}
	step = "completion"
	code, text, err := c.reply(2 * time.Minute)
	if err != nil {
		return err
	}
	if code != 226 && code != 250 {
		return &ftpError{code, text}
	}
	c.cmd(221, 5*time.Second, "QUIT")
	return nil
}

func mqttProjectFile(ctx context.Context, p *PrinterConfig, u *upload, plate, md5sum string) (string, error) {
	tlsConfig := p.tlsConfig(p.Serial)
	conn, err := dialTLS(ctx, p.Host, 8883, tlsConfig)
	if err != nil {
		return "", fmt.Errorf("uploaded, but the start command failed: %w", err)
	}
	defer conn.Close()
	fail := func(err error) (string, error) {
		if cause := context.Cause(ctx); cause != nil {
			err = cause
		}
		return "", fmt.Errorf("uploaded %s to the SD card, but starting the print failed: %w", u.filename, err)
	}
	r := bufio.NewReader(conn)
	id := make([]byte, 6)
	rand.Read(id)
	var connect []byte
	connect = append(connect, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 60)
	connect = append(connect, mqttString("orca-web-"+hex.EncodeToString(id))...)
	connect = append(connect, mqttString(bambuUser)...)
	connect = append(connect, mqttString(u.secret)...)
	conn.SetDeadline(time.Now().Add(15 * time.Second))
	if err := mqttWrite(conn, 0x10, connect); err != nil {
		return fail(netError(err, "the printer (MQTT port 8883)"))
	}
	kind, body, err := mqttRead(r)
	if err != nil {
		return fail(netError(err, "the printer (MQTT port 8883)"))
	}
	if kind != 2 || len(body) != 2 {
		return fail(errors.New("unexpected MQTT reply"))
	}
	switch body[1] {
	case 0:
	case 4, 5:
		return fail(errors.New("the printer rejected the access code over MQTT"))
	default:
		return fail(fmt.Errorf("the printer refused the MQTT connection (code %d)", body[1]))
	}
	report := "device/" + p.Serial + "/report"
	subscribe := append([]byte{0, 1}, mqttString(report)...)
	subscribe = append(subscribe, 0)
	if err := mqttWrite(conn, 0x82, subscribe); err != nil {
		return fail(err)
	}
	sequence := strconv.FormatInt(time.Now().UnixMilli()%1_000_000_000, 10)
	name := strings.TrimSuffix(u.filename, ".gcode.3mf")
	command, _ := json.Marshal(map[string]any{"print": map[string]any{
		"sequence_id": sequence, "command": "project_file", "param": plate,
		"project_id": "0", "profile_id": "0", "task_id": "0", "subtask_id": "0", "subtask_name": name,
		"file": u.filename, "url": "file:///sdcard/" + u.filename, "md5": md5sum,
		"timelapse": false, "bed_type": "auto", "bed_leveling": true, "flow_cali": false,
		"vibration_cali": true, "layer_inspect": false, "use_ams": false, "ams_mapping": []int{},
	}})
	publish := append(mqttString("device/"+p.Serial+"/request"), command...)
	if err := mqttWrite(conn, 0x30, publish); err != nil {
		return fail(err)
	}
	conn.SetDeadline(time.Now().Add(20 * time.Second))
	for {
		kind, body, err := mqttRead(r)
		if err != nil {
			if os.IsTimeout(err) {
				mqttWrite(conn, 0xE0, nil)
				return "Uploaded " + u.filename + " and sent the start command; the printer did not confirm within 20 s, so check its screen.", nil
			}
			return fail(err)
		}
		if kind != 3 || len(body) < 2 {
			continue
		}
		topicLength := int(binary.BigEndian.Uint16(body))
		if 2+topicLength > len(body) {
			continue
		}
		var message struct {
			Print struct {
				Command   string `json:"command"`
				Sequence  string `json:"sequence_id"`
				Result    string `json:"result"`
				Reason    string `json:"reason"`
				State     string `json:"gcode_state"`
				Subtask   string `json:"subtask_name"`
				ErrorCode int    `json:"print_error"`
				GcodeFile string `json:"gcode_file"`
			} `json:"print"`
		}
		if json.Unmarshal(body[2+topicLength:], &message) != nil {
			continue
		}
		m := message.Print
		if m.Command == "project_file" && m.Sequence == sequence {
			if strings.EqualFold(m.Result, "success") {
				mqttWrite(conn, 0xE0, nil)
				return "Uploaded " + u.filename + " to " + p.Name + " and started printing.", nil
			}
			reason := m.Reason
			if reason == "" {
				reason = m.Result
			}
			return fail(fmt.Errorf("the printer refused the job: %s", reason))
		}
		if (m.State == "PREPARE" || m.State == "RUNNING") && (m.Subtask == name || m.GcodeFile == u.filename) {
			mqttWrite(conn, 0xE0, nil)
			return "Uploaded " + u.filename + " to " + p.Name + " and started printing.", nil
		}
	}
}

func mqttString(s string) []byte {
	return append(binary.BigEndian.AppendUint16(nil, uint16(len(s))), s...)
}

func mqttWrite(w io.Writer, header byte, body []byte) error {
	packet := []byte{header}
	n := len(body)
	for {
		b := byte(n % 128)
		n /= 128
		if n > 0 {
			b |= 0x80
		}
		packet = append(packet, b)
		if n == 0 {
			break
		}
	}
	_, err := w.Write(append(packet, body...))
	return err
}

func mqttRead(r *bufio.Reader) (byte, []byte, error) {
	header, err := r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	length, shift := 0, 0
	for i := 0; ; i++ {
		b, err := r.ReadByte()
		if err != nil {
			return 0, nil, err
		}
		length |= int(b&0x7f) << shift
		shift += 7
		if b&0x80 == 0 {
			break
		}
		if i == 3 {
			return 0, nil, errors.New("malformed MQTT length")
		}
	}
	if length > 4<<20 {
		return 0, nil, errors.New("MQTT packet too large")
	}
	body := make([]byte, length)
	if _, err := io.ReadFull(r, body); err != nil {
		return 0, nil, err
	}
	return header >> 4, body, nil
}

func printerFingerprint(address, resources string) error {
	conn, err := tls.DialWithDialer(&net.Dialer{Timeout: 10 * time.Second}, "tcp", address, &tls.Config{InsecureSkipVerify: true})
	if err != nil {
		return err
	}
	defer conn.Close()
	chain := conn.ConnectionState().PeerCertificates
	leaf := chain[0]
	sum := sha256.Sum256(leaf.Raw)
	fmt.Printf("subject:     %s\nissuer:      %s\nvalid:       %s to %s\nfingerprint: sha256:%s\n",
		leaf.Subject, leaf.Issuer, leaf.NotBefore.Format(time.DateOnly), leaf.NotAfter.Format(time.DateOnly), hex.EncodeToString(sum[:]))
	if pem, err := os.ReadFile(resources + "/cert/printer.cer"); err == nil {
		roots := x509.NewCertPool()
		roots.AppendCertsFromPEM(pem)
		intermediates := x509.NewCertPool()
		for _, cert := range chain[1:] {
			intermediates.AddCert(cert)
		}
		_, verr := leaf.Verify(x509.VerifyOptions{Roots: roots, Intermediates: intermediates, KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny}})
		fmt.Printf("bambu-ca:    %v (common name %q)\n", verr == nil, leaf.Subject.CommonName)
	}
	return nil
}
