/*
 * Plugin glue for Shorten.
 *   Copyright (c) 2006 William Pitcock <nenolod -at- nenolod.net>.
 *   Copyright (c) 2006 Thomas Cort <tcort -at- cs.ubishops.ca>.
 *
 * Based on Plugins/Input/wma/allcodecs.c,
 *   Copyright (c) 2002 Fabrice Bellard.
 *   Copyright (c) 2004 Roman Bogorodskiy.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "avcodec.h"
#include "avformat.h"

void _avcodec_register_all(void)
{
	static int inited = 0;
    
    	if (inited != 0)
		return;
    	inited = 1;

	register_avcodec(&shorten_decoder);
}

void _av_register_all(void)
{
        puts("calling avcodec_init()\n");
	avcodec_init();
        puts("calling avcodec_register_all()\n");
	_avcodec_register_all();
        puts("calling raw_init()\n");
	_raw_init();

	/* file protocols */
	register_protocol(&file_protocol);
	register_protocol(&pipe_protocol);
}

