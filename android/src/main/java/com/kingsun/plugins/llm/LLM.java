package com.kingsun.plugins.llm;

import com.getcapacitor.Logger;

public class LLM {

    public String echo(String value) {
        Logger.info("Echo", value);
        return value;
    }
}
