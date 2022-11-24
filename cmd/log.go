package cmd

import (
	log "github.com/sirupsen/logrus"
)

func ConfigureLogging() error {
	l, err := log.ParseLevel(Opts.LogLevel)
	if err != nil {
		return err
	}
	log.SetLevel(l)
	log.SetFormatter(&log.TextFormatter{
		ForceColors:   true,
		FullTimestamp: true,
	})
	return nil
}
