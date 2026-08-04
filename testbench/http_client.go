package main

import (
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
)

func httpCmd(args []string) {
	fs := flag.NewFlagSet("http", flag.ExitOnError)
	url := fs.String("url", "http://10.200.0.2:8080/", "URL to fetch")
	fs.Parse(args)

	resp, err := http.Get(*url)
	if err != nil {
		fmt.Fprintf(os.Stderr, "request error: %v\n", err)
		os.Exit(1)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		fmt.Fprintf(os.Stderr, "read body error: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Status: %s\n", resp.Status)
	fmt.Printf("Body (%d bytes):\n%s\n", len(body), string(body))
}
